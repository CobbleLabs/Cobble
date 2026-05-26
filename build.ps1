#Requires -Version 5.1
<#
.SYNOPSIS
    Cobble build pipeline.

.DESCRIPTION
    .\build.ps1                          Debug build (dev/test, no Themida)
    .\build.ps1 -PrepareRelease          Release + Themida + signed manifest
    .\build.ps1 -PrepareRelease -Deploy  Same, then scp to VPS
    .\build.ps1 -Clean                   Wipe out/, generated/, hash headers
    .\build.ps1 -SkipDLL                 Loader-only rebuild
#>

param(
    [switch]$Clean,
    [switch]$SkipDLL,
    [switch]$PrepareRelease,
    [switch]$Deploy,
    [string]$ReleaseNotes = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$BuildStart = Get-Date

# --- Logging -----------------------------------------------------------------

function Write-Log {
    param(
        [string]$Message,
        [ValidateSet("STEP","OK","WARN","FAIL","INFO")]
        [string]$Level = "INFO"
    )
    $time  = Get-Date -Format "HH:mm:ss"
    $color = switch ($Level) {
        "STEP" { "Cyan"    }
        "OK"   { "Green"   }
        "WARN" { "Yellow"  }
        "FAIL" { "Red"     }
        default{ "Gray"    }
    }
    $tag = $Level.PadRight(4)
    Write-Host "[$time][$tag] $Message" -ForegroundColor $color
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
        "/v:minimal" "/nologo"
    if ($LASTEXITCODE -ne 0) { Write-Log "$Label build failed" FAIL; exit 1 }
}

function Get-SHA256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
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

# --- Paths -------------------------------------------------------------------

$Configuration = if ($PrepareRelease) { "Release" } else { "Debug" }
$Platform      = "x64"

# Auto-detect MSBuild via vswhere - works on Community, Professional, Enterprise
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
    Root           = $PSScriptRoot
    MSBuild        = $msbuildExe
    DllProject     = "$PSScriptRoot\Cobble.vcxproj"
    LoaderProject  = "$PSScriptRoot\loader\CobbleLoader.vcxproj"
    DllOutput      = "$PSScriptRoot\out\bin\$Configuration\Cobble.dll"
    LoaderOutput   = "$PSScriptRoot\out\bin\$Configuration\CobbleLoader.exe"
    EncOutput      = "$PSScriptRoot\loader\res\Cobble.enc"
    DllHashHeader  = "$PSScriptRoot\loader\src\util\dll_hash.h"
    IntegrityHeader= "$PSScriptRoot\loader\src\security\integrity_gen.hpp"
    VersionHeader  = "$PSScriptRoot\generated\version.h"
    ConfigJson     = "$PSScriptRoot\cobble.json"
    ReleaseDir     = "$PSScriptRoot\out\releases\latest"
    PrivateKey     = "$PSScriptRoot\infra\keys\private.key"
    ThemidaDir     = "$PSScriptRoot\themida"
    SignScript     = "$PSScriptRoot\tools\sign_manifest.go"
}

# --- Banner ------------------------------------------------------------------

Clear-Host
Write-Host ""
Write-Host "  +==================================+" -ForegroundColor DarkGray
Write-Host "  |     COBBLE BUILD PIPELINE        |" -ForegroundColor Cyan
Write-Host "  +==================================+" -ForegroundColor DarkGray
Write-Host "  Config : $Configuration | Platform: $Platform"
Write-Host "  Release: $PrepareRelease | Deploy: $Deploy"
Write-Host ""

# --- Clean -------------------------------------------------------------------

if ($Clean) {
    Write-Log "Cleaning build artifacts..." STEP
    @("$($Paths.Root)\out\bin", "$($Paths.Root)\out\obj", "$($Paths.Root)\loader\out") |
        Where-Object { Test-Path $_ } | ForEach-Object { Remove-Item -Recurse -Force $_ }
    if (Test-Path $Paths.DllHashHeader)   { Remove-Item -Force $Paths.DllHashHeader }
    if (Test-Path $Paths.IntegrityHeader) { Remove-Item -Force $Paths.IntegrityHeader }
    Write-Log "Clean complete" OK
}

# --- Config generation -------------------------------------------------------

#region Config

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

#endregion

# --- DLL build ---------------------------------------------------------------

#region DLL

if (-not $SkipDLL) {
    Invoke-MSBuild $Paths.DllProject "Cobble.dll ($Configuration)"
    Assert-File $Paths.DllOutput "Cobble.dll missing after build"
    Write-Log "Cobble.dll built  [$($Paths.DllOutput)]" OK
}

#endregion

# --- Payload encryption ------------------------------------------------------

#region Encryption

Write-Log "Encrypting DLL payload (AES-256-CBC)..." STEP
$resDir = Split-Path $Paths.EncOutput -Parent
if (-not (Test-Path $resDir)) { New-Item -ItemType Directory -Path $resDir -Force | Out-Null }

& "$($Paths.Root)\tools\encrypt_payload.ps1" `
    -InputDll  $Paths.DllOutput `
    -OutputFile $Paths.EncOutput `
    -PayloadKey $env:PAYLOAD_KEY `
    -Quiet

Assert-File $Paths.EncOutput "Payload encryption failed"
Write-Log "Payload encrypted  [$([Math]::Round((Get-Item $Paths.EncOutput).Length/1KB)) KB]" OK

#endregion

# --- DLL hash header ---------------------------------------------------------

#region DLL Hash

Write-Log "Computing DLL hash..." STEP
Assert-File $Paths.DllOutput "Cobble.dll not found - build it first or remove -SkipDLL"

$dllHash      = Get-SHA256 $Paths.DllOutput
$dllSize      = (Get-Item $Paths.DllOutput).Length
$buildTs      = [int][double]::Parse((Get-Date -UFormat %s))

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

#endregion

# --- Loader build (two-pass for self-integrity) -------------------------------

#region Loader

function Build-Loader {
    param([string]$Pass)
    Invoke-MSBuild $Paths.LoaderProject "CobbleLoader.exe ($Pass)"
    Assert-File $Paths.LoaderOutput "CobbleLoader.exe missing after $Pass"
    Write-Log "CobbleLoader.exe built  [$Pass]" OK
}

Build-Loader "Pass 1"

# Compute chunked integrity hashes of Pass-1 binary
Write-Log "Computing loader integrity hashes..." STEP
$loaderBytes = [System.IO.File]::ReadAllBytes($Paths.LoaderOutput)
$loaderSize  = $loaderBytes.Length
$chunk       = [Math]::Floor($loaderSize / 3)
$h1 = Get-ChunkHash $loaderBytes 0       $chunk
$h2 = Get-ChunkHash $loaderBytes $chunk  $chunk
$h3 = Get-ChunkHash $loaderBytes ($chunk*2) ($loaderSize - $chunk*2)
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

#endregion

# --- Release packaging -------------------------------------------------------

#region Release

if ($PrepareRelease) {

    Write-Log "Starting release packaging..." STEP
    $version = Get-Version

    # Pre-flight checks
    Assert-File $Paths.PrivateKey "infra/keys/private.key not found - get it from the team lead"

    $ThemidaExe = $null
    foreach ($c in @("$($Paths.ThemidaDir)\Themida64.exe", "$($Paths.ThemidaDir)\Themida.exe")) {
        if (Test-Path $c) { $ThemidaExe = $c; break }
    }
    if (-not $ThemidaExe) {
        Write-Log "Themida not found in themida/ - run: git submodule update --init themida" FAIL; exit 1
    }
    Assert-File "$($Paths.ThemidaDir)\TMLicenseA1.dat" "TMLicenseA1.dat missing from themida/ submodule"
    Assert-File "$($Paths.ThemidaDir)\cobble.tmd"      "cobble.tmd missing from themida/ submodule"

    # Themida - patch .tmd with current repo paths, run via cmd start /w
    Write-Log "Applying Themida protection (~30-60s)..." STEP
    $ProtectedLoader = "$($Paths.Root)\out\bin\Release\CobbleLoader_protected.exe"
    $TmdPatched      = "$($Paths.Root)\out\bin\Release\_cobble_patched.tmd"
    New-Item -ItemType Directory -Force -Path (Split-Path $TmdPatched) | Out-Null

    (Get-Content "$($Paths.ThemidaDir)\cobble.tmd" -Raw) `
        -replace 'INPUT_FILE_NAME=.*',  "INPUT_FILE_NAME=$($Paths.LoaderOutput)" `
        -replace 'OUTPUT_FILE_NAME=.*', "OUTPUT_FILE_NAME=$ProtectedLoader" |
        Set-Content $TmdPatched -NoNewline

    if (Test-Path $ProtectedLoader) { Remove-Item $ProtectedLoader -Force }

    # Oreans docs: "start /w Themida /protect project.tmd" - blocks until done
    $bat = "$($Paths.Root)\out\bin\Release\_run_themida.bat"
    @('@echo off', "start /w `"`" `"$ThemidaExe`" /protect `"$TmdPatched`"", 'exit /b %errorlevel%') |
        Set-Content -Path $bat -Encoding ASCII
    & cmd.exe /c $bat
    $themidaExit = $LASTEXITCODE
    Remove-Item $bat      -ErrorAction SilentlyContinue
    Remove-Item $TmdPatched -ErrorAction SilentlyContinue

    $themidaErrors = @{
        1="invalid project file"; 2="cannot open input"; 3="already protected"
        4="SecureEngine macro error"; 5="fatal protection error"
        6="cannot write output"; 7="splash file error"; 8="taggant certificate error"
    }
    if ($themidaErrors.ContainsKey($themidaExit)) {
        Write-Log "Themida failed: $($themidaErrors[$themidaExit])" FAIL; exit 1
    }
    # STATUS_CONTROL_C_EXIT (-1073741510): user closed CMD after Themida finished - check output
    if (-not (Test-Path $ProtectedLoader)) {
        Write-Log "Themida produced no output (exit=$themidaExit)" FAIL; exit 1
    }
    Copy-Item $ProtectedLoader $Paths.LoaderOutput -Force
    Remove-Item $ProtectedLoader -ErrorAction SilentlyContinue
    Write-Log "Themida protection applied" OK

    # Assemble release directory
    New-Item -ItemType Directory -Force -Path $Paths.ReleaseDir | Out-Null
    Copy-Item $Paths.LoaderOutput "$($Paths.ReleaseDir)\CobbleLoader.exe" -Force
    Copy-Item $Paths.DllOutput    "$($Paths.ReleaseDir)\Cobble.dll"       -Force

    $loaderHash = Get-SHA256 "$($Paths.ReleaseDir)\CobbleLoader.exe"
    $loaderSize = (Get-Item "$($Paths.ReleaseDir)\CobbleLoader.exe").Length

    $isMandatory = ($Host.UI.PromptForChoice("Release", "Mark as MANDATORY update?",
        @("&Yes","&No"), 1) -eq 0)

    $manifest = [ordered]@{
        version       = $version
        min_allowed   = "1.0.0"
        mandatory     = $isMandatory
        protected     = $true
        loader_sha256 = $loaderHash
        loader_size   = $loaderSize
        dll_sha256    = $dllHash
        dll_size      = $dllSize
        notes         = $ReleaseNotes
    }
    $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes(($manifest | ConvertTo-Json -Compress))
    [System.IO.File]::WriteAllBytes("$($Paths.ReleaseDir)\manifest.json", $manifestBytes)

    # Sign manifest - tools/sign_manifest.go is the canonical signer
    Write-Log "Signing manifest with Ed25519..." STEP
    Assert-File $Paths.SignScript "tools/sign_manifest.go not found"
    go run $Paths.SignScript `
        -key      $Paths.PrivateKey `
        -manifest "$($Paths.ReleaseDir)\manifest.json" `
        -out      "$($Paths.ReleaseDir)\manifest.sig"
    if ($LASTEXITCODE -ne 0) { Write-Log "Manifest signing failed - is Go installed?" FAIL; exit 1 }
    Assert-File "$($Paths.ReleaseDir)\manifest.sig" "manifest.sig not produced"
    Write-Log "Release v$version signed" OK
}

#endregion

# --- Deploy ------------------------------------------------------------------

#region Deploy

if ($Deploy) {
    if (-not $PrepareRelease) {
        Write-Log "-Deploy requires -PrepareRelease" FAIL; exit 1
    }
    Write-Log "Deploying to VPS..." STEP

    $vpsHost    = $cobbleConfig.deploy.vps_host
    $vpsUser    = $cobbleConfig.deploy.vps_user
    $sshKey     = $cobbleConfig.deploy.ssh_key -replace '~', $env:USERPROFILE
    $remotePath = $cobbleConfig.deploy.releases_path

    $sshOpts = @("-o","StrictHostKeyChecking=no","-o","UserKnownHostsFile=NUL","-o","LogLevel=ERROR","-i",$sshKey)

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

#endregion

# --- Summary -----------------------------------------------------------------

$elapsed = (Get-Date) - $BuildStart

Write-Host ""
Write-Host "  +==================================+" -ForegroundColor DarkGray
Write-Host "  |       BUILD SUCCEEDED            |" -ForegroundColor Green
Write-Host "  +==================================+" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Artifacts"
Write-Host "  ---------"
Write-Host "  DLL    : $($Paths.DllOutput)"
Write-Host "  Loader : $($Paths.LoaderOutput)"
Write-Host "  Hash   : $($dllHash.Substring(0,16))..."
Write-Host ""

if ($PrepareRelease) {
    Write-Host "  Release" -ForegroundColor Green
    Write-Host "  -------" -ForegroundColor Green
    Write-Host "  Version    : $(Get-Version)" -ForegroundColor Green
    Write-Host "  Themida    : APPLIED" -ForegroundColor Green
    Write-Host "  Manifest   : SIGNED" -ForegroundColor Green
    Write-Host "  Deployable : YES" -ForegroundColor Green
    if ($Deploy) {
        Write-Host "  Deployed   : YES" -ForegroundColor Green
    }
} else {
    Write-Host "  [WARN] DEBUG BUILD - Themida: SKIPPED - NOT deployable" -ForegroundColor Yellow
}

Write-Host ""
$elapsed_str = $elapsed.ToString("mm\:ss")
Write-Host "  Build time: $elapsed_str" -ForegroundColor DarkGray
Write-Host ""
