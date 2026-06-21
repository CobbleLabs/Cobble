#Requires -Version 5.1
<#
.SYNOPSIS
    Cobble build pipeline.

.DESCRIPTION
    .\build.ps1                          Interactive mode selector
    .\build.ps1 -Build                  Plain build (dev, no VMProtect, no signed manifest)
    .\build.ps1 -Pack                    Build + VMProtect Ultra on DLL/loader + signed manifest
    .\build.ps1 -Pack -Protector None    Production build + signed manifest, no built-in protector
    .\build.ps1 -SignExisting -Deploy    Sign/deploy externally protected binaries
    .\build.ps1 -Pack -Deploy            Pack + scp to VPS
    .\build.ps1 -Clean                   Wipe out/, generated/, hash headers
    .\build.ps1 -SkipDLL                 Loader-only rebuild

.NOTES
    There is no longer a separate "Debug" configuration. Every build is Release.
    `-Pack` builds production-mode binaries and signs a release manifest.
    `-Protector VMProtect` applies the in-repo VMProtect Ultra profile to both
    Cobble.dll and CobbleLoader.exe.
    `-Protector None` leaves the production binaries unprotected.
#>

param(
    [switch]$Clean,
    [switch]$SkipDLL,
    [switch]$Build,
    [switch]$Pack,
    [ValidateSet("VMProtect", "None")][string]$Protector = "VMProtect",
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

if ($SignExisting -and ($Build -or $Pack -or $SkipDLL)) {
    Write-Log "-SignExisting cannot be combined with -Build, -Pack, or -SkipDLL" FAIL
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
        Write-Log "Applying VMProtect Ultra to $Label (mutation + virtualization)..." STEP

        & $Paths.VMProtectExe $InputPath $protectedOutput -pf $Paths.VMProtectProject -we
        $vmProtectExit = $LASTEXITCODE
        if ($vmProtectExit -ne 0) {
            Write-Log "VMProtect failed for $Label (exit=$vmProtectExit)" FAIL
            exit 1
        }
        if (-not (Test-Path $protectedOutput)) {
            Write-Log "VMProtect produced no protected $Label" FAIL
            exit 1
        }

        Copy-Item -LiteralPath $protectedOutput -Destination $InputPath -Force
        Write-Log "VMProtect Ultra applied to $Label [$((Get-Item $InputPath).Length) bytes]" OK
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
    $content | Out-File -FilePath "$outDir\build_flags.h" -Encoding ASCII
}

# --- Interactive mode selector -----------------------------------------------

Clear-Host
Write-Host ""
Write-Host "  +----------------------------------+" -ForegroundColor Cyan
Write-Host "  |     COBBLE BUILD PIPELINE        |" -ForegroundColor Cyan
Write-Host "  +----------------------------------+" -ForegroundColor Cyan
Write-Host ""

if (-not $Clean -and -not $Build -and -not $Pack -and -not $SkipDLL -and -not $SignExisting) {
    Write-Host "  Select build mode:" -ForegroundColor Cyan
    Write-Host "    [1]  Build (dev, no VMProtect)" -ForegroundColor Green
    Write-Host "    [2]  Build + Pack (VMProtect Ultra + signed manifest)" -ForegroundColor Yellow
    Write-Host "    [3]  Build + Pack + Deploy" -ForegroundColor Blue
    Write-Host "    [4]  Sign/deploy externally protected binaries" -ForegroundColor Magenta
    Write-Host "    [5]  Clean" -ForegroundColor Red
    Write-Host ""
    $choice = Read-Host "  Choice [1-4]"
    switch ($choice.Trim()) {
        "1" { $Build = $true }
        "2" { $Pack = $true }
        "3" { $Pack = $true; $Deploy = $true }
        "4" { $SignExisting = $true; $Deploy = $true }
        "5" { $Clean = $true }
        default {
            Write-Host "  Invalid choice. Defaulting to plain Build." -ForegroundColor Yellow
            $Build = $true
        }
    }
    Write-Host ""
}

$Configuration = "Release"
$Platform = "x64"
$BuildMode = if ($Pack -or $SignExisting) { "Pack" } else { "Dev" }

Write-Host "  Config : $Configuration | Platform: $Platform | Mode: $BuildMode"
Write-Host "  Pack   : $Pack | Protector: $Protector | SignExisting: $SignExisting | Deploy: $Deploy"
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
    VMProtectDir     = "$PSScriptRoot\vmprotect"
    VMProtectBuild   = "$PSScriptRoot\vmprotect\build.ps1"
    VMProtectExe     = "$PSScriptRoot\vmprotect\bin\64\Ultimate\VMProtectCon.exe"
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
Write-Log "Writing generated/build_flags.h (DevBuild=$([bool]$DevBuild))..." STEP
Write-BuildFlags -DevBuild:$DevBuild

if ($Pack -and $Protector -eq "VMProtect") {
    Assert-File $Paths.VMProtectBuild "VMProtect submodule is missing - run: git submodule update --init --recursive"
    Write-Log "Building VMProtect console and SDK..." STEP
    & $Paths.VMProtectBuild -Platform x64
    if ($LASTEXITCODE -ne 0) { Write-Log "VMProtect build failed" FAIL; exit 1 }
    Assert-File $Paths.VMProtectExe "VMProtectCon.exe was not produced"
}

# --- Config generation -------------------------------------------------------

Write-Log "Generating config headers from cobble.json..." STEP
Assert-File $Paths.ConfigJson "cobble.json not found - copy cobble.json.example and fill in values"

& "$($Paths.Root)\generate_config.ps1" -ConfigPath $Paths.ConfigJson
Assert-File "$($Paths.Root)\generated\server_config.h" "Config generation failed"

$cobbleConfig = Get-Content $Paths.ConfigJson -Raw | ConvertFrom-Json
if (-not $env:PAYLOAD_KEY) { $env:PAYLOAD_KEY = $cobbleConfig.payload_key }
if (-not $env:PAYLOAD_KEY) {
    Write-Log "PAYLOAD_KEY not set - add payload_key to cobble.json or set the env var" FAIL; exit 1
}
Write-Log "Config headers generated" OK

function New-SignedRelease {
    param(
        [string]$SourceLoader,
        [string]$SourceDll,
        [bool]$Protected
    )

    Assert-File $Paths.PrivateKey "infra/keys/private.key not found - get it from the team lead"
    Assert-File $SourceLoader "CobbleLoader.exe not found at $SourceLoader"
    Assert-File $SourceDll "Cobble.dll not found at $SourceDll"

    $version = Get-Version

    New-Item -ItemType Directory -Force -Path $Paths.ReleaseDir | Out-Null
    $releaseLoader = "$($Paths.ReleaseDir)\CobbleLoader.exe"
    $releaseDll = "$($Paths.ReleaseDir)\Cobble.dll"

    Copy-Item -LiteralPath $SourceLoader -Destination $releaseLoader -Force
    Copy-Item -LiteralPath $SourceDll    -Destination $releaseDll    -Force

    $loaderHash = Get-SHA256 $releaseLoader
    $loaderSize = (Get-Item $releaseLoader).Length
    $releaseDllHash = Get-SHA256 $releaseDll
    $releaseDllSize = (Get-Item $releaseDll).Length

    $isMandatory = $false
    if ($Mandatory) {
        $isMandatory = $true
    }
    else {
        $isMandatory = ($Host.UI.PromptForChoice("Release", "Mark as MANDATORY update?",
                @("&Yes", "&No"), 1) -eq 0)
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
    [System.IO.File]::WriteAllBytes("$($Paths.ReleaseDir)\manifest.json", $manifestBytes)

    Write-Log "Signing manifest with Ed25519..." STEP
    Assert-File $Paths.SignScript "tools/sign_manifest.go not found"
    go run $Paths.SignScript `
        -key      $Paths.PrivateKey `
        -manifest "$($Paths.ReleaseDir)\manifest.json" `
        -out      "$($Paths.ReleaseDir)\manifest.sig"
    if ($LASTEXITCODE -ne 0) { Write-Log "Manifest signing failed - is Go installed?" FAIL; exit 1 }
    Assert-File "$($Paths.ReleaseDir)\manifest.sig" "manifest.sig not produced"
    Write-Log "Release v$version signed" OK

    return [pscustomobject]@{
        LoaderPath = $releaseLoader
        DllPath    = $releaseDll
        LoaderHash = $loaderHash
        DllHash    = $releaseDllHash
        DllSize    = $releaseDllSize
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
if ($Pack -and $Protector -eq "VMProtect") {
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

# --- Pack: VMProtect Ultra + signed manifest ---------------------------------

if ($Pack) {

    Write-Log "Starting Pack pipeline (protector=$Protector + signed manifest)..." STEP
    $version = Get-Version

    if ($Protector -eq "VMProtect") {
        Assert-File $Paths.VMProtectProject "VMProtect Ultra project is missing"

        # Temp files — always cleaned up via finally
        New-Item -ItemType Directory -Force -Path $Paths.TempDir | Out-Null
        $ProtectedLoader = "$($Paths.TempDir)\CobbleLoader_protected.exe"

        try {
            Write-Log "Applying VMProtect Ultra to CobbleLoader.exe (mutation + virtualization)..." STEP
            if (Test-Path $ProtectedLoader) { Remove-Item $ProtectedLoader -Force }

            & $Paths.VMProtectExe $Paths.LoaderOutput $ProtectedLoader -pf $Paths.VMProtectProject -we
            $vmProtectExit = $LASTEXITCODE
            if ($vmProtectExit -ne 0) {
                Write-Log "VMProtect failed (exit=$vmProtectExit)" FAIL; exit 1
            }
            if (-not (Test-Path $ProtectedLoader)) {
                Write-Log "VMProtect produced no output" FAIL; exit 1
            }

            Copy-Item $ProtectedLoader $Paths.LoaderOutput -Force
            Write-Log "VMProtect Ultra applied to CobbleLoader.exe [$((Get-Item $Paths.LoaderOutput).Length) bytes]" OK

        }
        finally {
            # Always clean up temp files regardless of success or failure
            @($ProtectedLoader) | ForEach-Object {
                if (Test-Path $_) { Remove-Item $_ -Force -ErrorAction SilentlyContinue }
            }
            if ((Get-ChildItem $Paths.TempDir -ErrorAction SilentlyContinue | Measure-Object).Count -eq 0) {
                Remove-Item $Paths.TempDir -Force -ErrorAction SilentlyContinue
            }
        }
    }
    else {
        Write-Log "Skipping VMProtect; this release must not be published unprotected" WARN
    }

    # Assemble release directory
    New-Item -ItemType Directory -Force -Path $Paths.ReleaseDir | Out-Null
    Copy-Item $Paths.LoaderOutput "$($Paths.ReleaseDir)\CobbleLoader.exe" -Force
    Copy-Item $Paths.DllOutput    "$($Paths.ReleaseDir)\Cobble.dll"       -Force

    $loaderHash = Get-SHA256 "$($Paths.ReleaseDir)\CobbleLoader.exe"
    $loaderSize = (Get-Item  "$($Paths.ReleaseDir)\CobbleLoader.exe").Length

    Assert-File $Paths.PrivateKey "infra/keys/private.key not found - protection succeeded, but signing requires the team key"

    $isMandatory = ($Host.UI.PromptForChoice("Release", "Mark as MANDATORY update?",
            @("&Yes", "&No"), 1) -eq 0)

    $manifest = [ordered]@{
        version       = $version
        min_allowed   = "1.0.0"
        mandatory     = $isMandatory
        protected     = ($Protector -ne "None")
        loader_sha256 = $loaderHash
        loader_size   = $loaderSize
        dll_sha256    = $dllHash
        dll_size      = $dllSize
        notes         = $ReleaseNotes
    }
    $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Compress))
    [System.IO.File]::WriteAllBytes("$($Paths.ReleaseDir)\manifest.json", $manifestBytes)

    Write-Log "Signing manifest with Ed25519..." STEP
    Assert-File $Paths.SignScript "tools/sign_manifest.go not found"
    go run $Paths.SignScript `
        -key      $Paths.PrivateKey `
        -manifest "$($Paths.ReleaseDir)\manifest.json" `
        -out      "$($Paths.ReleaseDir)\manifest.sig"
    if ($LASTEXITCODE -ne 0) { Write-Log "Manifest signing failed - is Go installed?" FAIL; exit 1 }
    Assert-File "$($Paths.ReleaseDir)\manifest.sig" "manifest.sig not produced"
    Write-Log "Pack v$version signed" OK
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
        $Paths.LoaderOutput
        $Paths.DllOutput
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
    Write-Host "  [INFO] DEV BUILD - VMProtect: SKIPPED - run with -Pack to ship" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "  Build time: $elapsed_str" -ForegroundColor DarkGray
Write-Host ""
