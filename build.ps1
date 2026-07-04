#Requires -Version 5.1
<#
.SYNOPSIS
    Cobble build pipeline.

.DESCRIPTION
    .\build.ps1                          Interactive mode selector
    .\build.ps1 -Build                  Plain build (dev, no VMProtect, no signed manifest)
    .\build.ps1 -Build -Obfuscate       Dev build + VMProtect Ultra, no signed manifest
    .\build.ps1 -Pack                    Build + VMProtect Ultra on DLL/loader + signed manifest
    .\build.ps1 -Pack -VMProtectRoot ..\cobble-vmprotect
                                           Use an external VMProtect source checkout
    .\build.ps1 -Pack -Protector None    Production build + signed manifest, no built-in protector
    .\build.ps1 -SignExisting -Deploy    Sign/deploy externally protected binaries
    .\build.ps1 -Pack -Deploy            Pack + scp to VPS
    .\build.ps1 -Clean                   Wipe out/, generated/, hash headers
    .\build.ps1 -SkipDLL                 Loader-only rebuild

.NOTES
    There is no longer a separate "Debug" configuration. Every build is Release.
    `-Pack` builds production-mode binaries and signs a release manifest.
    `-Obfuscate` keeps COBBLE_DEV_BUILD enabled while applying VMProtect Ultra.
    `-Protector VMProtect` applies the in-repo VMProtect Ultra profile to both
    Cobble.dll and CobbleLoader.exe.
    `-Protector None` leaves the production binaries unprotected.
#>

param(
    [switch]$Clean,
    [switch]$SkipDLL,
    [switch]$Build,
    [switch]$Obfuscate,
    [switch]$Pack,
    [ValidateSet("VMProtect", "None")][string]$Protector = "VMProtect",
    [string]$VMProtectRoot = "",
    [switch]$SignExisting,
    [string]$ExistingReleaseDir = "",
    [string]$ExistingLoader = "",
    [string]$ExistingDll = "",
    [switch]$Mandatory,
    [Alias("Release", "PrepareRelease")][switch]$ReleaseAlias,
    [switch]$Deploy,
    [string]$ReleaseNotes = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$BuildStart = Get-Date

# Backwards compat: -Release / -PrepareRelease still work and map to -Pack.
if ($ReleaseAlias) { $Pack = $true }

# --- Logging -----------------------------------------------------------------

function Write-Log {
    param(
        [string]$Message,
        [ValidateSet("STEP", "OK", "WARN", "FAIL", "INFO")]
        [string]$Level = "INFO"
    )
    $time = Get-Date -Format "HH:mm:ss"
    $color = switch ($Level) {
        "STEP" { "Cyan" }
        " OK " { "Green" }
        "WARN" { "Yellow" }
        "FAIL" { "Red" }
        default { "Gray" }
    }
    $tag = $Level.PadRight(4)
    Write-Host "[$time][$tag] $Message" -ForegroundColor $color
}

if ($SignExisting -and ($Build -or $Obfuscate -or $Pack -or $SkipDLL)) {
    Write-Log "-SignExisting cannot be combined with -Build, -Obfuscate, -Pack, or -SkipDLL" FAIL
    exit 1
}
if ($Obfuscate -and $Protector -ne "VMProtect") {
    Write-Log "-Obfuscate requires -Protector VMProtect" FAIL
    exit 1
}
if ($Pack -and $Protector -eq "None" -and $Deploy) {
    Write-Log "-Pack -Protector None -Deploy would publish unprotected binaries." FAIL
    exit 1
}

# --- Helpers -----------------------------------------------------------------

function Assert-File {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) { Write-Log $Message FAIL; exit 1 }
}

function Invoke-MSBuild {
    param([string]$Project, [string]$Label)
    Write-Log "Building $Label..." STEP
    & $Paths.MSBuild $Project `
        "/p:Configuration=$Configuration" `
        "/p:Platform=$Platform" `
        "/p:CobbleBuildMode=$BuildMode" `
        "/v:minimal" "/nologo"
    if ($LASTEXITCODE -ne 0) { Write-Log "$Label build failed" FAIL; exit 1 }
}

function Get-SHA256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
}

# Write $Content to $Path only if it differs from what's already there. Returns
# $true if it wrote, $false if the file was already up to date.
#
# This matters because several generated headers are force-included into every
# translation unit (build_flags.h) or pulled in by many TUs (generated/*.h). A
# plain Out-File updates the file's mtime even when the bytes are identical,
# which makes MSBuild treat every dependent TU as dirty and recompile the whole
# project. Comparing content first keeps incremental builds incremental.
function Write-IfChanged {
    param([string]$Path, [string]$Content, [string]$Encoding = "UTF8")
    if (Test-Path -LiteralPath $Path) {
        $existing = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        if ($null -ne $existing -and
            $existing.TrimEnd("`r", "`n") -eq $Content.TrimEnd("`r", "`n")) {
            return $false
        }
    }
    $Content | Out-File -FilePath $Path -Encoding $Encoding
    return $true
}

function Assert-ProtectedPE {
    param(
        [string]$OriginalHash,
        [string]$OutputPath,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $OutputPath)) {
        Write-Log "VMProtect produced no protected $Label" FAIL
        exit 1
    }

    $outputInfo = Get-Item -LiteralPath $OutputPath
    if ($outputInfo.Length -lt 2) {
        Write-Log "VMProtect produced an empty or truncated $Label" FAIL
        exit 1
    }

    $stream = [System.IO.File]::Open(
        $OutputPath,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )
    try {
        $first = $stream.ReadByte()
        $second = $stream.ReadByte()
    }
    finally {
        $stream.Dispose()
    }

    if ($first -ne 0x4D -or $second -ne 0x5A) {
        Write-Log "VMProtect output for $Label is not a valid PE file" FAIL
        exit 1
    }

    $protectedHash = Get-SHA256 $OutputPath
    if ($protectedHash -eq $OriginalHash) {
        Write-Log "VMProtect output for $Label is byte-identical to the input" FAIL
        exit 1
    }

    return $protectedHash
}

function Protect-Artifact {
    param(
        [string]$InputPath,
        [string]$OutputName,
        [string]$Label
    )

    Assert-File $InputPath "$Label was not produced"
    Assert-File $Paths.VMProtectProject "VMProtect Ultra project is missing"

    New-Item -ItemType Directory -Force -Path $Paths.TempDir | Out-Null
    $protectedOutput = Join-Path $Paths.TempDir $OutputName

    try {
        if (Test-Path $protectedOutput) { Remove-Item $protectedOutput -Force }
        $originalHash = Get-SHA256 $InputPath
        Write-Log "Applying VMProtect Ultra to $Label (mutation + virtualization)..." STEP

        & $Paths.VMProtectExe $InputPath $protectedOutput -pf $Paths.VMProtectProject -we
        $vmProtectExit = $LASTEXITCODE
        if ($vmProtectExit -ne 0) {
            Write-Log "VMProtect failed for $Label (exit=$vmProtectExit)" FAIL
            exit 1
        }
        $protectedHash = Assert-ProtectedPE $originalHash $protectedOutput $Label
        Copy-Item -LiteralPath $protectedOutput -Destination $InputPath -Force
        Write-Log "VMProtect Ultra applied to $Label [$((Get-Item $InputPath).Length) bytes, SHA256=$protectedHash]" OK
    }
    finally {
        if (Test-Path $protectedOutput) {
            Remove-Item $protectedOutput -Force -ErrorAction SilentlyContinue
        }
        if ((Test-Path $Paths.TempDir) -and
            (Get-ChildItem $Paths.TempDir -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0) {
            Remove-Item $Paths.TempDir -Force -ErrorAction SilentlyContinue
        }
    }
}

function Get-ChunkHash {
    param([byte[]]$Bytes, [int]$Offset, [int]$Length)
    $chunk = New-Object byte[] $Length
    [Array]::Copy($Bytes, $Offset, $chunk, 0, $Length)
    $hash = [System.Security.Cryptography.SHA256]::Create().ComputeHash($chunk)
    return "0x" + [BitConverter]::ToUInt32($hash, 0).ToString("X8")
}

function Get-Version {
    if (Test-Path $Paths.VersionHeader) {
        $content = Get-Content $Paths.VersionHeader -Raw
        if ($content -match '#define\s+COBBLE_VERSION\s+"([^"]+)"') { return $matches[1] }
    }
    return "0.0.0"
}

# Write generated/build_flags.h. ForcedIncludeFiles in the vcxproj pulls this
# into every translation unit, so source code can branch on COBBLE_DEV_BUILD
# without any extra plumbing. Pack builds drop the macro entirely.
function Write-BuildFlags {
    param([bool]$DevBuild)
    $outDir = "$($Paths.Root)\generated"
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
    $content = if ($DevBuild) {
        @"
#pragma once
// AUTO-GENERATED by build.ps1 - DO NOT EDIT
// This file is force-included into every TU via vcxproj <ForcedIncludeFiles>.
// COBBLE_DEV_BUILD gates ONLY cosmetic dev features (file
// tracer, plaintext OBFUSCATE_STRING). It MUST NOT gate validation,
// handshake, integrity, or any other security-relevant code path.
#define COBBLE_DEV_BUILD 1
"@
    }
    else {
        @"
#pragma once
// AUTO-GENERATED by build.ps1 - DO NOT EDIT
// Pack build: COBBLE_DEV_BUILD intentionally undefined.
"@
    }
    # Force-included into every TU: touching its mtime would recompile the whole
    # project, so only rewrite when the flag actually flips (Dev <-> Pack).
    return (Write-IfChanged -Path "$outDir\build_flags.h" -Content $content -Encoding ASCII)
}

# --- Interactive mode selector -----------------------------------------------

Clear-Host
Write-Host ""
Write-Host "  +----------------------------------+" -ForegroundColor Cyan
Write-Host "  |     COBBLE BUILD PIPELINE        |" -ForegroundColor Cyan
Write-Host "  +----------------------------------+" -ForegroundColor Cyan
Write-Host ""

if (-not $Clean -and -not $Build -and -not $Obfuscate -and -not $Pack -and -not $SkipDLL -and -not $SignExisting) {
    Write-Host "  Select build mode:" -ForegroundColor Cyan
    Write-Host "    [1]  Build without obfuscation" -ForegroundColor Green
    Write-Host "    [2]  Build with VMProtect obfuscation" -ForegroundColor Yellow
    Write-Host "    [3]  Build + Pack (VMProtect Ultra + signed manifest)" -ForegroundColor Yellow
    Write-Host "    [4]  Build + Pack + Deploy" -ForegroundColor Blue
    Write-Host "    [5]  Sign/deploy externally protected binaries" -ForegroundColor Magenta
    Write-Host "    [6]  Clean" -ForegroundColor Red
    Write-Host ""
    $choice = Read-Host "  Choice [1-6]"
    switch ($choice.Trim()) {
        "1" { $Build = $true }
        "2" { $Build = $true; $Obfuscate = $true }
        "3" { $Pack = $true }
        "4" { $Pack = $true; $Deploy = $true }
        "5" { $SignExisting = $true; $Deploy = $true }
        "6" { $Clean = $true }
        default {
            Write-Log "Invalid choice. Select a value from 1 to 6." FAIL
            exit 1
        }
    }
    Write-Host ""
}

$Configuration = "Release"
$Platform = "x64"
$BuildMode = if ($Pack -or $SignExisting) { "Pack" } else { "Dev" }
$ProtectArtifacts = (($Pack -or $Obfuscate) -and $Protector -eq "VMProtect")

Write-Host "  Config : $Configuration | Platform: $Platform | Mode: $BuildMode"
Write-Host "  Pack   : $Pack | Obfuscate: $Obfuscate | Protector: $Protector | SignExisting: $SignExisting | Deploy: $Deploy"
Write-Host ""

# --- Paths -------------------------------------------------------------------

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuildExe = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    if ($vsPath) { $msbuildExe = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe" }
}
if (-not $msbuildExe -or -not (Test-Path $msbuildExe)) {
    $msbuildExe = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
}
if (-not (Test-Path $msbuildExe)) {
    Write-Log "MSBuild not found. Install Visual Studio 2022 with C++ workload." FAIL; exit 1
}

$vmProtectCandidate = if ($VMProtectRoot) {
    if ([IO.Path]::IsPathRooted($VMProtectRoot)) {
        $VMProtectRoot
    } else {
        Join-Path $PSScriptRoot $VMProtectRoot
    }
} else {
    Join-Path $PSScriptRoot "vmprotect"
}
$vmProtectPath = [IO.Path]::GetFullPath($vmProtectCandidate)

$Paths = @{
    Root             = $PSScriptRoot
    MSBuild          = $msbuildExe
    DllProject       = "$PSScriptRoot\Cobble.vcxproj"
    LoaderProject    = "$PSScriptRoot\loader\CobbleLoader.vcxproj"
    DllOutput        = "$PSScriptRoot\out\bin\$BuildMode\Cobble.dll"
    LoaderOutput     = "$PSScriptRoot\out\bin\$BuildMode\CobbleLoader.exe"
    EncOutput        = "$PSScriptRoot\loader\res\Cobble.enc"
    DllHashHeader    = "$PSScriptRoot\loader\src\util\dll_hash.h"
    IntegrityHeader  = "$PSScriptRoot\loader\src\security\integrity_gen.hpp"
    VersionHeader    = "$PSScriptRoot\generated\version.h"
    BuildFlagsHeader = "$PSScriptRoot\generated\build_flags.h"
    ConfigJson       = "$PSScriptRoot\cobble.json"
    ReleaseDir       = "$PSScriptRoot\out\releases\latest"
    PrivateKey       = "$PSScriptRoot\infra\keys\private.key"
    VMProtectDir     = $vmProtectPath
    VMProtectBuild   = Join-Path $vmProtectPath "build.ps1"
    VMProtectExe     = Join-Path $vmProtectPath "bin\64\Ultimate\VMProtectCon.exe"
    VMProtectProject = "$PSScriptRoot\protection\cobble-ultra.vmp"
    SignScript       = "$PSScriptRoot\tools\sign_manifest.go"
    TempDir          = "$PSScriptRoot\out\bin\$BuildMode\_tmp"
}

# --- Clean -------------------------------------------------------------------

if ($Clean) {
    Write-Log "Cleaning build artifacts..." STEP
    @("$($Paths.Root)\out\bin", "$($Paths.Root)\out\obj", "$($Paths.Root)\loader\out") |
    Where-Object { Test-Path $_ } | ForEach-Object { Remove-Item -Recurse -Force $_ }
    if (Test-Path $Paths.DllHashHeader) { Remove-Item -Force $Paths.DllHashHeader }
    if (Test-Path $Paths.IntegrityHeader) { Remove-Item -Force $Paths.IntegrityHeader }
    if (Test-Path $Paths.BuildFlagsHeader) { Remove-Item -Force $Paths.BuildFlagsHeader }
    Write-Log "Clean complete (out/bin/{Dev,Pack}, out/obj, generated headers)" OK
    exit 0
}

# --- Build flags header (force-included into every TU) -----------------------

$DevBuild = (-not $Pack -and -not $SignExisting)
$flagsChanged = Write-BuildFlags -DevBuild:$DevBuild
if ($flagsChanged) {
    Write-Log "Wrote generated/build_flags.h (DevBuild=$([bool]$DevBuild))" STEP
}
else {
    Write-Log "build_flags.h unchanged (DevBuild=$([bool]$DevBuild)) - not rewritten" OK
}

if ($ProtectArtifacts) {
    Assert-File $Paths.VMProtectBuild "VMProtect source is missing at $($Paths.VMProtectDir)"
    Write-Log "Building VMProtect console and SDK from $($Paths.VMProtectDir)..." STEP
    & $Paths.VMProtectBuild -Platform x64
    if ($LASTEXITCODE -ne 0) { Write-Log "VMProtect build failed" FAIL; exit 1 }
    Assert-File $Paths.VMProtectExe "VMProtectCon.exe was not produced"
}

# --- Config generation -------------------------------------------------------

Write-Log "Generating config headers from cobble.json..." STEP
Assert-File $Paths.ConfigJson "cobble.json not found - copy cobble.json.example and fill in values"

# Regenerating rewrites generated/*.h with fresh random XOR masks, which dirties
# every TU that includes them. Pack builds MUST regenerate every time (the
# per-build polymorphic masks are a security property). Dev builds may reuse the
# cached headers when cobble.json is unchanged and all four already exist -- the
# masks don't matter for a dev build, so this avoids needless recompilation.
$generatedDir = "$($Paths.Root)\generated"
$configStamp = "$generatedDir\.config_stamp"
$generatedHeaders = @("server_config.h", "keys.h", "payload_key.h", "version.h") |
    ForEach-Object { "$generatedDir\$_" }
$configHash = Get-SHA256 $Paths.ConfigJson

$missingHeaders = @($generatedHeaders | Where-Object { -not (Test-Path $_) }).Count
$stampValue = if (Test-Path $configStamp) {
    $raw = Get-Content $configStamp -Raw -ErrorAction SilentlyContinue
    if ($raw) { $raw.Trim() } else { "" }
}
else { "" }

$canReuseConfig = $DevBuild -and ($missingHeaders -eq 0) -and ($stampValue -eq $configHash)

if ($canReuseConfig) {
    Write-Log "Config headers unchanged - reusing cached generated/*.h" OK
}
else {
    & "$($Paths.Root)\generate_config.ps1" -ConfigPath $Paths.ConfigJson
    Assert-File "$generatedDir\server_config.h" "Config generation failed"
    $configHash | Out-File -FilePath $configStamp -Encoding ASCII
    Write-Log "Config headers generated" OK
}

$cobbleConfig = Get-Content $Paths.ConfigJson -Raw | ConvertFrom-Json
if (-not $env:PAYLOAD_KEY) { $env:PAYLOAD_KEY = $cobbleConfig.payload_key }
if (-not $env:PAYLOAD_KEY) {
    Write-Log "PAYLOAD_KEY not set - add payload_key to cobble.json or set the env var" FAIL; exit 1
}

function New-SignedRelease {
    param(
        [string]$SourceLoader,
        [string]$SourceDll,
        [bool]$Protected
    )

    Assert-File $Paths.PrivateKey "infra/keys/private.key not found - get it from the team lead"
    Assert-File $Paths.SignScript "tools/sign_manifest.go not found"
    Assert-File $SourceLoader "CobbleLoader.exe not found at $SourceLoader"
    Assert-File $SourceDll "Cobble.dll not found at $SourceDll"

    $version = Get-Version

    $releaseParent = Split-Path -Parent $Paths.ReleaseDir
    New-Item -ItemType Directory -Force -Path $releaseParent | Out-Null
    $stageDir = Join-Path $releaseParent (".staging-" + [Guid]::NewGuid().ToString("N"))
    $stageDir = [IO.Path]::GetFullPath($stageDir)
    $releasePrefix = [IO.Path]::GetFullPath($releaseParent).TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
    if (-not $stageDir.StartsWith($releasePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Log "Refusing to stage a release outside $releaseParent" FAIL
        exit 1
    }

    try {
        New-Item -ItemType Directory -Path $stageDir | Out-Null
        $stageLoader = Join-Path $stageDir "CobbleLoader.exe"
        $stageDll = Join-Path $stageDir "Cobble.dll"
        $stageManifest = Join-Path $stageDir "manifest.json"
        $stageSignature = Join-Path $stageDir "manifest.sig"

        Copy-Item -LiteralPath $SourceLoader -Destination $stageLoader -Force
        Copy-Item -LiteralPath $SourceDll    -Destination $stageDll    -Force

        $loaderHash = Get-SHA256 $stageLoader
        $loaderSize = (Get-Item $stageLoader).Length
        $releaseDllHash = Get-SHA256 $stageDll
        $releaseDllSize = (Get-Item $stageDll).Length

        $isMandatory = if ($Mandatory) {
            $true
        }
        else {
            $Host.UI.PromptForChoice("Release", "Mark as MANDATORY update?",
                @("&Yes", "&No"), 1) -eq 0
        }

        $manifest = [ordered]@{
            version       = $version
            min_allowed   = "1.0.0"
            mandatory     = $isMandatory
            protected     = $Protected
            loader_sha256 = $loaderHash
            loader_size   = $loaderSize
            dll_sha256    = $releaseDllHash
            dll_size      = $releaseDllSize
            notes         = $ReleaseNotes
        }
        $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Compress))
        [System.IO.File]::WriteAllBytes($stageManifest, $manifestBytes)

        Write-Log "Signing staged manifest with Ed25519..." STEP
        go run $Paths.SignScript `
            -key      $Paths.PrivateKey `
            -manifest $stageManifest `
            -out      $stageSignature
        if ($LASTEXITCODE -ne 0) {
            Write-Log "Manifest signing failed - is Go installed?" FAIL
            exit 1
        }
        Assert-File $stageSignature "manifest.sig not produced"

        # Prepare the local deployment bundle only after all four staged
        # artifacts exist and signing succeeds. This does not publish a GitHub
        # release; option 3 uploads this bundle directly to the configured VPS.
        New-Item -ItemType Directory -Force -Path $Paths.ReleaseDir | Out-Null
        foreach ($name in @("CobbleLoader.exe", "Cobble.dll", "manifest.json", "manifest.sig")) {
            Copy-Item -LiteralPath (Join-Path $stageDir $name) `
                -Destination (Join-Path $Paths.ReleaseDir $name) -Force
        }
        Write-Log "Deployment bundle v$version signed and prepared locally" OK

        return [pscustomobject]@{
            LoaderPath = Join-Path $Paths.ReleaseDir "CobbleLoader.exe"
            DllPath    = Join-Path $Paths.ReleaseDir "Cobble.dll"
            LoaderHash = $loaderHash
            DllHash    = $releaseDllHash
            DllSize    = $releaseDllSize
        }
    }
    finally {
        if (Test-Path -LiteralPath $stageDir) {
            Remove-Item -LiteralPath $stageDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Deploy-Release {
    param([string[]]$Files)

    Write-Log "Deploying to VPS..." STEP

    $vpsHost = $cobbleConfig.deploy.vps_host
    $vpsUser = $cobbleConfig.deploy.vps_user
    $sshKey = $cobbleConfig.deploy.ssh_key -replace '~', $env:USERPROFILE
    $remotePath = $cobbleConfig.deploy.releases_path

    $sshOpts = @("-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=NUL", "-o", "LogLevel=ERROR", "-i", $sshKey)

    & ssh @sshOpts "${vpsUser}@${vpsHost}" "mkdir -p '$remotePath'"
    if ($LASTEXITCODE -ne 0) { Write-Log "Cannot reach remote dir $remotePath" FAIL; exit 1 }

    & scp @sshOpts $Files "${vpsUser}@${vpsHost}:${remotePath}/"
    if ($LASTEXITCODE -ne 0) { Write-Log "scp failed" FAIL; exit 1 }
    Write-Log "Deployed $($Files.Count) files to ${vpsUser}@${vpsHost}:${remotePath}" OK
}

if ($SignExisting) {
    $existingDir = if ($ExistingReleaseDir) { $ExistingReleaseDir } else { "$PSScriptRoot\out\vmprotect" }
    $sourceLoader = if ($ExistingLoader) { $ExistingLoader } else { Join-Path $existingDir "CobbleLoader.exe" }
    $sourceDll = if ($ExistingDll) { $ExistingDll } else { Join-Path $existingDir "Cobble.dll" }

    Write-Log "Signing externally protected binaries..." STEP
    Write-Log "Loader: $sourceLoader" INFO
    Write-Log "DLL   : $sourceDll" INFO

    $releaseInfo = New-SignedRelease -SourceLoader $sourceLoader -SourceDll $sourceDll -Protected:$true
    if ($Deploy) {
        Deploy-Release -Files @(
            $releaseInfo.LoaderPath,
            $releaseInfo.DllPath,
            "$($Paths.ReleaseDir)\manifest.json",
            "$($Paths.ReleaseDir)\manifest.sig"
        )
    }

    $elapsed = (Get-Date) - $BuildStart
    $elapsed_str = $elapsed.ToString("mm\:ss")

    Write-Host ""
    Write-Host "  +----------------------------------+" -ForegroundColor Green
    Write-Host "  |       SIGN/DEPLOY SUCCEEDED      |" -ForegroundColor Green
    Write-Host "  +----------------------------------+" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Release"
    Write-Host "  -------"
    Write-Host "  DLL      : $($releaseInfo.DllPath)"
    Write-Host "  Loader   : $($releaseInfo.LoaderPath)"
    Write-Host "  Hash     : $($releaseInfo.DllHash.Substring(0,16))..."
    Write-Host "  Manifest : SIGNED" -ForegroundColor Green
    if ($Deploy) {
        Write-Host "  Deployed : YES" -ForegroundColor Green
    }
    Write-Host ""
    Write-Host "  Build time: $elapsed_str" -ForegroundColor DarkGray
    Write-Host ""
    exit 0
}

# --- DLL build ---------------------------------------------------------------

if (-not $SkipDLL) {
    Invoke-MSBuild $Paths.DllProject "Cobble.dll ($Configuration)"
    Assert-File $Paths.DllOutput "Cobble.dll missing after build"
    Write-Log "Cobble.dll built  [$($Paths.DllOutput)]" OK
}

# Protect the DLL before hashing and encrypting it into the loader. This keeps
# the embedded payload, release DLL, and signed manifest byte-for-byte aligned.
if ($ProtectArtifacts) {
    Protect-Artifact `
        -InputPath $Paths.DllOutput `
        -OutputName "Cobble_protected.dll" `
        -Label "Cobble.dll"
}

# --- Payload encryption ------------------------------------------------------

Write-Log "Encrypting DLL payload (AES-256-CBC)..." STEP
$resDir = Split-Path $Paths.EncOutput -Parent
if (-not (Test-Path $resDir)) { New-Item -ItemType Directory -Path $resDir -Force | Out-Null }

& "$($Paths.Root)\tools\encrypt_payload.ps1" `
    -InputDll   $Paths.DllOutput `
    -OutputFile $Paths.EncOutput `
    -PayloadKey $env:PAYLOAD_KEY `
    -Quiet

Assert-File $Paths.EncOutput "Payload encryption failed"
Write-Log "Payload encrypted  [$([Math]::Round((Get-Item $Paths.EncOutput).Length/1KB)) KB]" OK

# --- DLL hash header ---------------------------------------------------------

Write-Log "Computing DLL hash..." STEP
Assert-File $Paths.DllOutput "Cobble.dll not found"

$dllHash = Get-SHA256 $Paths.DllOutput
$dllSize = (Get-Item $Paths.DllOutput).Length
$buildTs = [int][double]::Parse((Get-Date -UFormat %s))

@"
#pragma once
// Auto-generated by build.ps1 - do not edit
#define DLL_HASH_ENABLED      true
#define DLL_EXPECTED_HASH     "$dllHash"
#define DLL_EXPECTED_SIZE     $dllSize
#define DLL_BUILD_TIMESTAMP   $buildTs
#define DLL_HASH_CHUNK1       "$($dllHash.Substring(0,  16))"
#define DLL_HASH_CHUNK2       "$($dllHash.Substring(16, 16))"
#define DLL_HASH_CHUNK3       "$($dllHash.Substring(32, 16))"
#define DLL_HASH_CHUNK4       "$($dllHash.Substring(48, 16))"
"@ | Out-File -FilePath $Paths.DllHashHeader -Encoding UTF8

Write-Log "dll_hash.h written  [$($dllHash.Substring(0,16))...]" OK

# --- Loader build (two-pass for self-integrity) ------------------------------

function Build-Loader {
    param([string]$Pass)
    Invoke-MSBuild $Paths.LoaderProject "CobbleLoader.exe ($Pass)"
    Assert-File $Paths.LoaderOutput "CobbleLoader.exe missing after $Pass"
    Write-Log "CobbleLoader.exe built  [$Pass]" OK
}

# Pass 1 includes integrity_gen.hpp (it lives in the source tree). If a
# clean wiped it, drop a zero-filled placeholder so Pass 1 can compile;
# Pass 2 will overwrite it with the real chunk hashes.
if (-not (Test-Path $Paths.IntegrityHeader)) {
    @"
#pragma once
#include <cstdint>
#include <cstddef>
// Placeholder generated by build.ps1 - overwritten by Pass 2.
namespace integrity_data {
    constexpr uint32_t EXPECTED_HASHES[] = { 0x00000000, 0x00000000, 0x00000000 };
    constexpr size_t   NUM_CHUNKS = 3;
}
"@ | Out-File -FilePath $Paths.IntegrityHeader -Encoding UTF8
    Write-Log "integrity_gen.hpp placeholder written (Pass 1 prereq)" INFO
}

Build-Loader "Pass 1"

Write-Log "Computing loader integrity hashes..." STEP
$loaderBytes = [System.IO.File]::ReadAllBytes($Paths.LoaderOutput)
$loaderSize = $loaderBytes.Length
$chunk = [Math]::Floor($loaderSize / 3)
$h1 = Get-ChunkHash $loaderBytes 0            $chunk
$h2 = Get-ChunkHash $loaderBytes $chunk       $chunk
$h3 = Get-ChunkHash $loaderBytes ($chunk * 2) ($loaderSize - $chunk * 2)
Write-Log "Chunks: $h1  $h2  $h3" INFO

@"
#pragma once
#include <cstdint>
#include <cstddef>
// Auto-generated by build.ps1 - do not edit
namespace integrity_data {
    constexpr uint32_t EXPECTED_HASHES[] = { $h1, $h2, $h3 };
    constexpr size_t   NUM_CHUNKS = 3;
}
"@ | Out-File -FilePath $Paths.IntegrityHeader -Encoding UTF8
Write-Log "integrity_gen.hpp written" OK

Build-Loader "Pass 2"

# Protect the loader after its final build pass. Dev-obfuscated builds stop
# here without creating or signing a release manifest.
if ($ProtectArtifacts) {
    Assert-File $Paths.VMProtectProject "VMProtect Ultra project is missing"

    Protect-Artifact `
        -InputPath $Paths.LoaderOutput `
        -OutputName "CobbleLoader_protected.exe" `
        -Label "CobbleLoader.exe"
}

# --- Pack: signed manifest ----------------------------------------------------

if ($Pack) {

    Write-Log "Starting Pack pipeline (protector=$Protector + signed manifest)..." STEP
    $version = Get-Version

    if ($Protector -eq "None") {
        Write-Log "Skipping VMProtect; this release must not be published unprotected" WARN
    }

    $releaseInfo = New-SignedRelease `
        -SourceLoader $Paths.LoaderOutput `
        -SourceDll $Paths.DllOutput `
        -Protected:($Protector -ne "None")
}

# --- Deploy ------------------------------------------------------------------

if ($Deploy) {
    if (-not $Pack) {
        Write-Log "-Deploy requires -Pack" FAIL; exit 1
    }
    Write-Log "Deploying to VPS..." STEP

    $vpsHost = $cobbleConfig.deploy.vps_host
    $vpsUser = $cobbleConfig.deploy.vps_user
    $sshKey = $cobbleConfig.deploy.ssh_key -replace '~', $env:USERPROFILE
    $remotePath = $cobbleConfig.deploy.releases_path

    $sshOpts = @("-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=NUL", "-o", "LogLevel=ERROR", "-i", $sshKey)

    & ssh @sshOpts "${vpsUser}@${vpsHost}" "mkdir -p '$remotePath'"
    if ($LASTEXITCODE -ne 0) { Write-Log "Cannot reach remote dir $remotePath" FAIL; exit 1 }

    $files = @(
        "$($Paths.ReleaseDir)\CobbleLoader.exe"
        "$($Paths.ReleaseDir)\Cobble.dll"
        "$($Paths.ReleaseDir)\manifest.json"
        "$($Paths.ReleaseDir)\manifest.sig"
    )
    & scp @sshOpts $files "${vpsUser}@${vpsHost}:${remotePath}/"
    if ($LASTEXITCODE -ne 0) { Write-Log "scp failed" FAIL; exit 1 }
    Write-Log "Deployed $($files.Count) files to ${vpsUser}@${vpsHost}:${remotePath}" OK
}

# --- Summary -----------------------------------------------------------------

$elapsed = (Get-Date) - $BuildStart
$elapsed_str = $elapsed.ToString("mm\:ss")

Write-Host ""
Write-Host "  +----------------------------------+" -ForegroundColor Green
Write-Host "  |       BUILD SUCCEEDED            |" -ForegroundColor Green
Write-Host "  +----------------------------------+" -ForegroundColor Green

Write-Host ""
Write-Host "  Artifacts"
Write-Host "  ---------"
Write-Host "  DLL    : $($Paths.DllOutput)"
Write-Host "  Loader : $($Paths.LoaderOutput)"
Write-Host "  Hash   : $($dllHash.Substring(0,16))..."
Write-Host ""

if ($Pack) {
    Write-Host "  Pack" -ForegroundColor Green
    Write-Host "  -------" -ForegroundColor Green
    Write-Host "  Version    : $(Get-Version)" -ForegroundColor Green
    if ($Protector -eq "VMProtect") {
        Write-Host "  Protector  : VMProtect Ultra APPLIED (DLL + loader)" -ForegroundColor Green
    }
    else {
        Write-Host "  Protector  : NONE" -ForegroundColor Yellow
    }
    Write-Host "  Manifest   : SIGNED" -ForegroundColor Green
    $deployableLabel = if ($Protector -eq "VMProtect") { "YES" } else { "NO" }
    Write-Host "  Deployable : $deployableLabel" -ForegroundColor Green
    if ($Deploy) {
        Write-Host "  Deployed   : YES" -ForegroundColor Green
    }
}
else {
    if ($Obfuscate) {
        Write-Host "  [INFO] DEV BUILD - VMProtect Ultra APPLIED (DLL + loader) - not signed/deployable" -ForegroundColor Green
    }
    else {
        Write-Host "  [INFO] DEV BUILD - VMProtect: SKIPPED - run with -Obfuscate to test protected binaries" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "  Build time: $elapsed_str" -ForegroundColor DarkGray
Write-Host ""
