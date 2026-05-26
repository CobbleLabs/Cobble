# Cobble Build Script
#
#   .\build.ps1                    — Debug build (dev/test, no Themida, no manifest)
#   .\build.ps1 -PrepareRelease    — Release + Themida + signed manifest (only deployable build)
#   .\build.ps1 -PrepareRelease -Deploy  — same + scp to VPS
#   .\build.ps1 -Clean             — wipe out/, generated/, .enc, hash headers
#   .\build.ps1 -SkipDLL           — loader-only rebuild (skips DLL compile)

param(
    [switch]$Clean,
    [switch]$SkipDLL,
    [switch]$Verbose,
    [switch]$PrepareRelease,
    [switch]$Deploy,
    [string]$ReleaseNotes = ""
)

$ErrorActionPreference = "Stop"

# Paths
$RepoRoot = $PSScriptRoot
$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$Configuration = if ($PrepareRelease) { "Release" } else { "Debug" }
$Platform = "x64"

# Output paths
$DLLProject = "$RepoRoot\Cobble.vcxproj"
$LoaderProject = "$RepoRoot\loader\CobbleLoader.vcxproj"
$DLLOutput = "$RepoRoot\out\bin\$Configuration\Cobble.dll"
$HashHeader = "$RepoRoot\loader\src\util\dll_hash.h"
$LoaderOutput = "$RepoRoot\out\bin\$Configuration\CobbleLoader.exe"
$VersionHeader = "$RepoRoot\generated\version.h"

function Write-Step($msg) {
    Write-Host "`n[ BUILD ] " -ForegroundColor Cyan -NoNewline
    Write-Host $msg
}

function Write-Success($msg) {
    Write-Host "[ OK ] " -ForegroundColor Green -NoNewline
    Write-Host $msg
}

function Write-Fail($msg) {
    Write-Host "[ FAIL ] " -ForegroundColor Red -NoNewline
    Write-Host $msg
}

function Get-SHA256($filePath) {
    $hash = Get-FileHash -Path $filePath -Algorithm SHA256
    return $hash.Hash.ToLower()
}

function Get-FileSize($filePath) {
    return (Get-Item $filePath).Length
}

function Get-BuildTimestamp() {
    return [int][double]::Parse((Get-Date -UFormat %s))
}

function Get-Version() {
    if (Test-Path $VersionHeader) {
        $content = Get-Content $VersionHeader -Raw
        if ($content -match '#define\s+COBBLE_VERSION\s+"([^"]+)"') {
            return $matches[1]
        }
    }
    return "0.0.0"
}

# Clean if requested
if ($Clean) {
    Write-Step "Cleaning build artifacts..."
    if (Test-Path "$RepoRoot\out\bin") { Remove-Item -Recurse -Force "$RepoRoot\out\bin" }
    if (Test-Path "$RepoRoot\out\obj") { Remove-Item -Recurse -Force "$RepoRoot\out\obj" }
    if (Test-Path "$RepoRoot\loader\out") { Remove-Item -Recurse -Force "$RepoRoot\loader\out" }
    if (Test-Path $HashHeader) { Remove-Item -Force $HashHeader }
    Write-Success "Clean complete"
}

# Step 0: Generate config headers from cobble.json
Write-Step "Generating config headers..."
$configJson = "$RepoRoot\cobble.json"
if (!(Test-Path $configJson)) {
    Write-Fail "cobble.json not found! Copy cobble.json.example and fill in values."
    exit 1
}
& "$RepoRoot\generate_config.ps1" -ConfigPath $configJson
if (!(Test-Path "$RepoRoot\generated\server_config.h")) {
    Write-Fail "Config generation failed!"
    exit 1
}

# Read PAYLOAD_KEY from cobble.json (no more env var needed)
$cobbleConfig = Get-Content $configJson -Raw | ConvertFrom-Json
if (-not $env:PAYLOAD_KEY) {
    $env:PAYLOAD_KEY = $cobbleConfig.payload_key
}
Write-Success "Config generated (PAYLOAD_KEY from cobble.json)"

# Step 1: Build Cobble.dll
if (-not $SkipDLL) {
    Write-Step "Building Cobble.dll ($Configuration)..."
    
    $msbuildArgs = @(
        $DLLProject,
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/v:minimal",
        "/nologo"
    )
    
    & $MSBuild @msbuildArgs
    
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "Cobble.dll build failed!"
        exit 1
    }
    
    if (-not (Test-Path $DLLOutput)) {
        Write-Fail "Cobble.dll not found at expected path: $DLLOutput"
        exit 1
    }
    
    Write-Success "Cobble.dll built successfully"
}

# Step 2: Encrypt DLL payload
Write-Step "Encrypting DLL payload (AES-256-CBC)..."

$EncryptedOutput = "$RepoRoot\loader\res\Cobble.enc"

# Ensure the res directory exists
$resDir = Split-Path $EncryptedOutput -Parent
if (-not (Test-Path $resDir)) {
    New-Item -ItemType Directory -Path $resDir -Force | Out-Null
}

# Run encryption script - PAYLOAD_KEY is REQUIRED
if (-not $env:PAYLOAD_KEY) {
    Write-Fail "PAYLOAD_KEY not set! All builds require secure encryption."
    Write-Host "  Set: `$env:PAYLOAD_KEY = '<64-char-hex-key>'" -ForegroundColor Yellow
    Write-Host "  Generate with: openssl rand -hex 32" -ForegroundColor Yellow
    Write-Host "  Or set it in cobble.json (build.ps1 reads it from there)." -ForegroundColor Yellow
    exit 1
}

Write-Host "  Using PAYLOAD_KEY from environment (secure)" -ForegroundColor Green

& "$RepoRoot\tools\encrypt_payload.ps1" -InputDll $DLLOutput -OutputFile $EncryptedOutput -PayloadKey $env:PAYLOAD_KEY -Quiet

if (-not (Test-Path $EncryptedOutput)) {
    Write-Fail "Failed to encrypt DLL payload!"
    exit 1
}

$encryptedSize = Get-FileSize $EncryptedOutput
Write-Success "Encrypted payload: $EncryptedOutput ($encryptedSize bytes)"

# Step 3: Calculate DLL hash and metadata
Write-Step "Calculating DLL hash..."

if (-not (Test-Path $DLLOutput)) {
    Write-Fail "Cobble.dll not found! Build it first or don't use -SkipDLL"
    exit 1
}

$dllHash = Get-SHA256 $DLLOutput
$dllSize = Get-FileSize $DLLOutput
$buildTimestamp = Get-BuildTimestamp

Write-Success "Hash: $dllHash"
Write-Success "Size: $dllSize bytes"
Write-Success "Timestamp: $buildTimestamp"

# Step 4: Generate hash header
Write-Step "Generating dll_hash.h..."

$hashHeaderContent = @"
#pragma once
// Auto-generated by build.ps1 - DO NOT EDIT MANUALLY
// Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

#define DLL_HASH_ENABLED true
#define DLL_EXPECTED_HASH "$dllHash"
#define DLL_EXPECTED_SIZE $dllSize
#define DLL_BUILD_TIMESTAMP $buildTimestamp

// Chunked hash for obfuscation (optional use)
#define DLL_HASH_CHUNK1 "$($dllHash.Substring(0, 16))"
#define DLL_HASH_CHUNK2 "$($dllHash.Substring(16, 16))"
#define DLL_HASH_CHUNK3 "$($dllHash.Substring(32, 16))"
#define DLL_HASH_CHUNK4 "$($dllHash.Substring(48, 16))"
"@

$hashHeaderContent | Out-File -FilePath $HashHeader -Encoding UTF8
Write-Success "Generated $HashHeader"

# Step 5: Build Loader (First Pass - for hash calculation)
Write-Step "Building CobbleLoader.exe (Pass 1 - for hashing)..."

$msbuildArgs = @(
    $LoaderProject,
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/v:minimal",
    "/nologo"
)

& $MSBuild @msbuildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Fail "CobbleLoader.exe (Pass 1) build failed!"
    exit 1
}

if (-not (Test-Path $LoaderOutput)) {
    Write-Fail "CobbleLoader.exe not found at expected path: $LoaderOutput"
    exit 1
}

Write-Success "CobbleLoader.exe (Pass 1) built successfully"

# Step 6: Calculate Loader Integrity Hashes
Write-Step "Calculating loader integrity hashes..."

$loaderBytes = [System.IO.File]::ReadAllBytes($LoaderOutput)
$loaderSize = $loaderBytes.Length
$numChunks = 3
$chunkSize = [Math]::Floor($loaderSize / $numChunks)

# Use SHA256 and truncate to 32-bit for simplicity (avoids PowerShell int overflow)
function Get-ChunkHash($bytes, $offset, $length) {
    $chunk = New-Object byte[] $length
    [Array]::Copy($bytes, $offset, $chunk, 0, $length)
    
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hashBytes = $sha.ComputeHash($chunk)
    
    # Take first 4 bytes and convert to uint32
    $hash = [BitConverter]::ToUInt32($hashBytes, 0)
    return "0x" + $hash.ToString("X8")
}

$hash1 = Get-ChunkHash $loaderBytes 0 $chunkSize
$hash2 = Get-ChunkHash $loaderBytes $chunkSize $chunkSize
$hash3 = Get-ChunkHash $loaderBytes ($chunkSize * 2) ($loaderSize - ($chunkSize * 2))

Write-Success "Chunk 1 Hash: $hash1"
Write-Success "Chunk 2 Hash: $hash2"
Write-Success "Chunk 3 Hash: $hash3"

# Step 7: Generate integrity_gen.hpp with real hashes
Write-Step "Generating integrity_gen.hpp..."

$integrityHeader = "$RepoRoot\loader\src\security\integrity_gen.hpp"
$integrityContent = @"
#pragma once
#include <cstdint>
#include <cstddef>

// ============================================================================
// AUTO-GENERATED INTEGRITY HASHES
// ============================================================================
// 
// This file is automatically generated by build.ps1
// DO NOT EDIT MANUALLY
//
// Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
// Loader Size: $loaderSize bytes
// ============================================================================

namespace integrity_data {
    const uint32_t EXPECTED_HASHES[] = {
        $hash1,  // Chunk 0
        $hash2,  // Chunk 1
        $hash3   // Chunk 2
    };
    
    const size_t NUM_CHUNKS = sizeof(EXPECTED_HASHES) / sizeof(EXPECTED_HASHES[0]);
}
"@

$integrityContent | Out-File -FilePath $integrityHeader -Encoding UTF8
Write-Success "Generated $integrityHeader"

# Step 8: Build Loader (Second Pass - with real hashes)
Write-Step "Building CobbleLoader.exe (Pass 2 - with integrity hashes)..."

& $MSBuild @msbuildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Fail "CobbleLoader.exe (Pass 2) build failed!"
    exit 1
}

if (-not (Test-Path $LoaderOutput)) {
    Write-Fail "CobbleLoader.exe not found at expected path: $LoaderOutput"
    exit 1
}

Write-Success "CobbleLoader.exe built successfully"

# Step 5: Prepare Release (if requested)
if ($PrepareRelease) {
    Write-Step "Preparing signed release..."

    $version = Get-Version
    $releaseDir = "$RepoRoot\out\releases\latest"
    $privateKeyPath = "$RepoRoot\infra\keys\private.key"

    # Check for private key
    if (-not (Test-Path $privateKeyPath)) {
        Write-Fail "Private key not found at $privateKeyPath"
        Write-Host "  Run: .\tools\generate_keys.ps1" -ForegroundColor Yellow
        exit 1
    }

    # ── Themida enforcement ──────────────────────────────────────────────────
    # A -PrepareRelease build MUST go through Themida. No exceptions.
    # Themida binaries live in the themida/ submodule — no per-machine config needed.
    $ThemidaDir = "$RepoRoot\themida"

    $ThemidaExe = $null
    foreach ($candidate in @("$ThemidaDir\Themida64.exe", "$ThemidaDir\Themida.exe")) {
        if (Test-Path $candidate) { $ThemidaExe = $candidate; break }
    }
    if (-not $ThemidaExe) {
        Write-Fail "FATAL: Themida executable not found in themida/ submodule. Release aborted."
        Write-Host "  Run: git submodule update --init themida" -ForegroundColor Yellow
        Write-Host "  A -PrepareRelease build without Themida CANNOT be deployed." -ForegroundColor Red
        exit 1
    }
    $LicenseFile = "$ThemidaDir\TMLicenseA1.dat"
    if (-not (Test-Path $LicenseFile)) {
        Write-Fail "FATAL: TMLicenseA1.dat not found in themida/ submodule. Release aborted."
        exit 1
    }
    $TmdSource = "$ThemidaDir\cobble.tmd"
    if (-not (Test-Path $TmdSource)) {
        Write-Fail "FATAL: cobble.tmd not found in themida/ submodule. Release aborted."
        exit 1
    }

    Write-Step "Applying Themida protection..."
    Write-Host "  Themida: $ThemidaExe" -ForegroundColor Gray

    # Themida always uses the INPUT/OUTPUT paths baked into the .tmd, ignoring
    # any /inputfile /outputfile flags. The .tmd shipped in the submodule has
    # paths from the original dev machine, so rewrite them for this clone.
    $ProtectedLoader = "$RepoRoot\out\bin\Release\CobbleLoader_protected.exe"
    $TmdPatched      = "$RepoRoot\out\bin\Release\_cobble_patched.tmd"
    New-Item -ItemType Directory -Force -Path (Split-Path $TmdPatched) | Out-Null

    (Get-Content $TmdSource -Raw) `
        -replace 'INPUT_FILE_NAME=.*',  "INPUT_FILE_NAME=$LoaderOutput" `
        -replace 'OUTPUT_FILE_NAME=.*', "OUTPUT_FILE_NAME=$ProtectedLoader" |
        Set-Content $TmdPatched -NoNewline

    Write-Host "  Input:  $LoaderOutput" -ForegroundColor Gray
    Write-Host "  Output: $ProtectedLoader" -ForegroundColor Gray

    if (Test-Path $ProtectedLoader) { Remove-Item $ProtectedLoader -Force }

    # Run Themida through CMD using the canonical pattern from Oreans' docs:
    #
    #     start /w Themida /protect project.tmd
    #
    # `start /w` blocks until the spawned Themida process exits, and the BAT
    # propagates its errorlevel back via `exit /b`, so the build no longer
    # needs to poll for the output file. Documented Themida exit codes:
    #   0 success            | 1 invalid project file
    #   2 cannot open input  | 3 already protected
    #   4 SecureEngine macro | 5 fatal protection error
    #   6 cannot write       | 7 splash file error
    #   8 taggant certificate
    Write-Host "  Protecting via CMD (this takes ~30-60s)..." -ForegroundColor Gray

    $ThemidaBat = "$RepoRoot\out\bin\Release\_run_themida.bat"
    @(
        '@echo off'
        "start /w `"`" `"$ThemidaExe`" /protect `"$TmdPatched`""
        'exit /b %errorlevel%'
    ) | Set-Content -Path $ThemidaBat -Encoding ASCII

    & cmd.exe /c $ThemidaBat
    $themidaExit = $LASTEXITCODE

    Remove-Item $ThemidaBat -ErrorAction SilentlyContinue
    Remove-Item $TmdPatched -ErrorAction SilentlyContinue

    switch ($themidaExit) {
        0 { Write-Host "  Themida exit code 0 (protection successful)" -ForegroundColor Gray }
        1 { Write-Fail "Themida: project file does not exist or invalid"; exit 1 }
        2 { Write-Fail "Themida: file to protect cannot be opened";       exit 1 }
        3 { Write-Fail "Themida: file already protected";                 exit 1 }
        4 { Write-Fail "Themida: error in inserted SecureEngine macros";  exit 1 }
        5 { Write-Fail "Themida: fatal error while protecting file";      exit 1 }
        6 { Write-Fail "Themida: cannot write protected file to disk";    exit 1 }
        7 { Write-Fail "Themida: error opening/reading splash file";      exit 1 }
        8 { Write-Fail "Themida: taggant certificate cannot be applied";  exit 1 }
        default {
            # -1073741510 = STATUS_CONTROL_C_EXIT — user closed the CMD window after
            # Themida finished. Treat as success if the output file exists.
            if (Test-Path $ProtectedLoader) {
                Write-Host "  Themida window closed by user (output exists, treating as success)" -ForegroundColor Gray
            } else {
                Write-Fail "Themida: unknown exit code $themidaExit and no output produced"
                exit 1
            }
        }
    }

    if (-not (Test-Path $ProtectedLoader)) {
        Write-Fail "FATAL: Themida produced no output. Release aborted."
        Write-Host "  A -PrepareRelease build without Themida CANNOT be deployed." -ForegroundColor Red
        exit 1
    }
    # Copy protected binary over the unprotected one — manifest hashes will
    # reflect the exact artifact users run.
    Copy-Item $ProtectedLoader $LoaderOutput -Force
    Remove-Item $ProtectedLoader -ErrorAction SilentlyContinue
    Write-Success "Themida protection applied"

    # Create release directory
    New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

    # Copy binaries (now Themida-protected)
    Copy-Item $LoaderOutput "$releaseDir\CobbleLoader.exe" -Force
    Copy-Item $DLLOutput "$releaseDir\Cobble.dll" -Force
    
    # Calculate hashes
    $loaderHash = Get-SHA256 "$releaseDir\CobbleLoader.exe"
    $loaderSize = Get-FileSize "$releaseDir\CobbleLoader.exe"
    
    # Ask if mandatory
    $isMandatory = $false
    $resp = Read-Host "Is this release MANDATORY? (y/N)"
    if ($resp -eq 'y') { $isMandatory = $true }

    # Build manifest
    $manifest = @{
        version = $version
        min_allowed = "1.0.0"
        mandatory = $isMandatory
        protected = $true
        loader_sha256 = $loaderHash
        loader_size = $loaderSize
        dll_sha256 = $dllHash
        dll_size = $dllSize
        notes = $ReleaseNotes
    }
    
    $manifestJson = $manifest | ConvertTo-Json -Compress
    $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes($manifestJson)
    [System.IO.File]::WriteAllBytes("$releaseDir\manifest.json", $manifestBytes)
    
    # Sign manifest with Go (simple and reliable)
    $goSign = @"
package main
import (
    "crypto/ed25519"
    "fmt"
    "os"
)
func main() {
    key, err := os.ReadFile("$($privateKeyPath -replace '\\', '/')")
    if err != nil {
        fmt.Fprintln(os.Stderr, "read key:", err)
        os.Exit(1)
    }
    manifest, err := os.ReadFile("$($releaseDir -replace '\\', '/')/manifest.json")
    if err != nil {
        fmt.Fprintln(os.Stderr, "read manifest:", err)
        os.Exit(1)
    }
    var priv ed25519.PrivateKey
    switch len(key) {
    case ed25519.PrivateKeySize: // 64
        priv = ed25519.PrivateKey(key)
    case ed25519.SeedSize: // 32
        priv = ed25519.NewKeyFromSeed(key)
    default:
        fmt.Fprintf(os.Stderr, "unexpected key length: %d (want 32 seed or 64 priv)\n", len(key))
        os.Exit(1)
    }
    sig := ed25519.Sign(priv, manifest)
    if err := os.WriteFile("$($releaseDir -replace '\\', '/')/manifest.sig", sig, 0644); err != nil {
        fmt.Fprintln(os.Stderr, "write sig:", err)
        os.Exit(1)
    }
}
"@
    $goSign | Set-Content "$RepoRoot\temp_sign.go"
    Push-Location $RepoRoot
    go run temp_sign.go
    $signResult = $LASTEXITCODE
    Pop-Location
    Remove-Item "$RepoRoot\temp_sign.go" -ErrorAction SilentlyContinue
    
    if ($signResult -ne 0 -or -not (Test-Path "$releaseDir\manifest.sig")) {
        Write-Fail "Manifest signing failed! Make sure Go is installed."
        exit 1
    }
    
    Write-Success "Release v$version signed"
    Write-Host "  Files: $releaseDir" -ForegroundColor Gray
}

# Summary
Write-Host "`n" + "="*50 -ForegroundColor DarkGray
Write-Host "BUILD COMPLETE" -ForegroundColor Green
Write-Host "="*50 -ForegroundColor DarkGray
Write-Host "  DLL:    $DLLOutput"
Write-Host "  Loader: $LoaderOutput"
Write-Host "  Hash:   $($dllHash.Substring(0,16))..."
if ($PrepareRelease) {
    Write-Host "  Release: out\releases\latest\ (SIGNED)" -ForegroundColor Green
    Write-Host "[RELEASE] Themida: APPLIED | Manifest: SIGNED | Ready to deploy" -ForegroundColor Green
} elseif ($Release) {
    Write-Host "[RELEASE BUILD] Themida protection: SKIPPED. This build cannot be deployed." -ForegroundColor Yellow
    Write-Host "[DEV BUILD] Themida: SKIPPED | Not deployable" -ForegroundColor Yellow
} else {
    Write-Host "[DEBUG BUILD] Themida protection: SKIPPED. This build cannot be deployed." -ForegroundColor Yellow
    Write-Host "[DEV BUILD] Themida: SKIPPED | Not deployable" -ForegroundColor Yellow
}
Write-Host ""

# Deploy to VPS
if ($Deploy) {
    Write-Step "Deploying to VPS..."
    $cobbleConfig = Get-Content "$RepoRoot\cobble.json" -Raw | ConvertFrom-Json
    $vpsHost = $cobbleConfig.deploy.vps_host
    $vpsUser = $cobbleConfig.deploy.vps_user
    $sshKey = $cobbleConfig.deploy.ssh_key -replace '~', $env:USERPROFILE
    $remotePath = $cobbleConfig.deploy.releases_path

    # -o LogLevel=ERROR silences the "Permanently added to known_hosts" notice
    # that PowerShell otherwise treats as a fatal exception.
    $sshOpts = @(
        "-o", "StrictHostKeyChecking=no"
        "-o", "UserKnownHostsFile=NUL"
        "-o", "LogLevel=ERROR"
        "-i", $sshKey
    )

    # Make sure the remote dir exists (first deploy or after volume migration).
    & ssh @sshOpts "${vpsUser}@${vpsHost}" "mkdir -p '$remotePath'"
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "Could not ensure remote dir $remotePath"
        return
    }

    # Always push the binaries. If we just produced a signed manifest, push
    # it (and its signature) too so the loader's update flow stays consistent.
    $files = @($LoaderOutput, $DLLOutput)
    if ($PrepareRelease) {
        $manifestPath = Join-Path "$RepoRoot\out\releases\latest" "manifest.json"
        $sigPath = Join-Path "$RepoRoot\out\releases\latest" "manifest.sig"
        if ((Test-Path $manifestPath) -and (Test-Path $sigPath)) {
            $files += $manifestPath
            $files += $sigPath
        }
    }

    & scp @sshOpts $files "${vpsUser}@${vpsHost}:${remotePath}/"

    if ($LASTEXITCODE -eq 0) {
        Write-Success "Deployed $($files.Count) file(s) to ${vpsUser}@${vpsHost}:${remotePath}"
    } else {
        Write-Fail "Deploy failed (scp exit code: $LASTEXITCODE)"
    }
}
