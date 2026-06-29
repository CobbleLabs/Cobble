#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet("x64", "Win32")]
    [string]$Platform = "x64",
    [switch]$Clean,
    [switch]$SmokeTest
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

function Assert-PortableExecutable {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Expected PE output is missing: $Path"
    }
    if ((Get-Item -LiteralPath $Path).Length -lt 2) {
        throw "PE output is empty or truncated: $Path"
    }

    $stream = [IO.File]::Open(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read
    )
    try {
        $first = $stream.ReadByte()
        $second = $stream.ReadByte()
    }
    finally {
        $stream.Dispose()
    }

    if ($first -ne 0x4D -or $second -ne 0x5A) {
        throw "Output is not a valid PE file: $Path"
    }
}

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Build Tools not found (vswhere.exe is missing)."
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
    Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild was not found. Install Visual Studio 2022 C++ Build Tools."
}

$properties = @(
    "/p:Platform=$Platform",
    "/p:PlatformToolset=v143",
    "/p:WindowsTargetPlatformVersion=10.0",
    "/m",
    "/nologo",
    "/v:minimal"
)

$projects = @(
    @{ Path = "$root\vcproj\VMProtectSDK.vcxproj"; Configuration = "Release" },
    @{ Path = "$root\vcproj\VMProtectCon.vcxproj"; Configuration = "Ultimate" }
)

if ($Clean) {
    foreach ($project in $projects) {
        & $msbuild $project.Path "/t:Clean" "/p:Configuration=$($project.Configuration)" @properties
        if ($LASTEXITCODE -ne 0) {
            throw "Clean failed for $($project.Path)."
        }
    }
}

foreach ($project in $projects) {
    Write-Host "Building $([IO.Path]::GetFileName($project.Path)) ($($project.Configuration), $Platform)..."
    & $msbuild $project.Path "/t:Build" "/p:Configuration=$($project.Configuration)" @properties
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $($project.Path)."
    }
}

$architecture = if ($Platform -eq "x64") { "64" } else { "32" }
$consoleDir = "$root\bin\$architecture\Ultimate"
$sdkDir = "$root\bin\$architecture\Release"
$sdkRuntime = "$sdkDir\VMProtectSDK$architecture.dll"
$consoleRuntime = "$consoleDir\VMProtectSDK$architecture.dll"

# VMProtectCon imports the SDK runtime. Keep it next to the console so a
# checkout/build is immediately runnable without modifying PATH.
Copy-Item -LiteralPath $sdkRuntime -Destination $consoleRuntime -Force

$outputs = @(
    "$consoleDir\VMProtectCon.exe",
    $consoleRuntime,
    $sdkRuntime,
    "$sdkDir\VMProtectSDK$architecture.lib"
)

foreach ($output in $outputs) {
    if (-not (Test-Path -LiteralPath $output)) {
        throw "Expected build output is missing: $output"
    }
}

if ($SmokeTest) {
    $outRoot = [IO.Path]::GetFullPath((Join-Path $root "out"))
    $smokeRoot = [IO.Path]::GetFullPath((Join-Path $outRoot "smoke-test"))
    $outPrefix = $outRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $smokeRoot.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to use smoke-test path outside the build output directory: $smokeRoot"
    }

    $input = Join-Path $root "test-binaries\win32-app-test1-i386"
    $project = Join-Path $root "test-binaries\win32-app-test1-i386.vmp"
    $firstOutput = Join-Path $smokeRoot "protected-a.exe"
    $secondOutput = Join-Path $smokeRoot "protected-b.exe"

    foreach ($fixture in @($input, $project)) {
        if (-not (Test-Path -LiteralPath $fixture)) {
            throw "Smoke-test fixture is missing: $fixture"
        }
    }

    try {
        if (Test-Path -LiteralPath $smokeRoot) {
            Remove-Item -LiteralPath $smokeRoot -Recurse -Force
        }
        New-Item -ItemType Directory -Path $smokeRoot -Force | Out-Null
        $inputHash = (Get-FileHash -LiteralPath $input -Algorithm SHA256).Hash

        foreach ($protectedOutput in @($firstOutput, $secondOutput)) {
            & "$consoleDir\VMProtectCon.exe" $input $protectedOutput -pf $project -we
            if ($LASTEXITCODE -ne 0) {
                throw "VMProtect smoke test failed with exit code $LASTEXITCODE."
            }
            Assert-PortableExecutable $protectedOutput
            if ((Get-FileHash -LiteralPath $protectedOutput -Algorithm SHA256).Hash -eq $inputHash) {
                throw "VMProtect smoke test produced byte-identical output: $protectedOutput"
            }
        }

        $firstHash = (Get-FileHash -LiteralPath $firstOutput -Algorithm SHA256).Hash
        $secondHash = (Get-FileHash -LiteralPath $secondOutput -Algorithm SHA256).Hash
        if ($firstHash -eq $secondHash) {
            throw "Diversification smoke test failed: repeated protections are byte-identical."
        }

        Write-Host "VMProtect smoke test completed:"
        Write-Host "  first  SHA256: $firstHash"
        Write-Host "  second SHA256: $secondHash"
    }
    finally {
        if (Test-Path -LiteralPath $smokeRoot) {
            Remove-Item -LiteralPath $smokeRoot -Recurse -Force
        }
    }
}

Write-Host "VMProtect build completed:"
$outputs | ForEach-Object { Write-Host "  $_" }
