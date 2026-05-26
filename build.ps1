# Cobble Build Script
# Compiles DLL, calculates hash, embeds in loader, compiles loader
# Run from repository root: .\build.ps1

param(
    [switch]$Release,
    [switch]$Clean,
    [switch]$SkipDLL,
    [switch]$Verbose,
    [switch]$PrepareRelease,  # Also generate signed release in releases/latest/
    [switch]$Deploy,          # Upload release to VPS after build
    [string]$ReleaseNotes = ""
)

$ErrorActionPreference = "Stop"

# Paths
$RepoRoot = $PSScriptRoot
$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$Configuration = if ($Release -or $PrepareRelease) { "Release" } else { "Debug" }
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
    # If the SDK is missing, the manifest is never produced and the deploy
    # cannot proceed. This guarantees no unprotected binary ever reaches the
    # web from a -PrepareRelease invocation.

    # Themida path: read from cobble.json (per-machine, gitignored).
    # cobble.json.example has a "themida_path" key — set it to your local install.
    $ThemidaDir = $null
    if ($cobbleConfig.themida_path) {
        $ThemidaDir = $cobbleConfig.themida_path -replace '/', '\'
    }
    # Fallback: check submodule path (only works if Themida is installed there)
    if (-not $ThemidaDir -or -not (Test-Path $ThemidaDir)) {
        $ThemidaDir = "$RepoRoot\themida"
    }

    $ThemidaExe = $null
    foreach ($candidate in @("$ThemidaDir\Themida64.exe", "$ThemidaDir\Themida.exe")) {
        if (Test-Path $candidate) { $ThemidaExe = $candidate; break }
    }
    if (-not $ThemidaExe) {
        Write-Fail "FATAL: Themida executable not found. Release aborted."
        Write-Host "  Searched: $ThemidaDir" -ForegroundColor Red
        Write-Host "  Set `"themida_path`": `"D:\\Syra\\Cobble\\themida`" in cobble.json" -ForegroundColor Yellow
        Write-Host "  A -PrepareRelease build without Themida CANNOT be deployed." -ForegroundColor Red
        exit 1
    }

    $LicenseFile = "$ThemidaDir\TMLicenseA1.dat"
    if (-not (Test-Path $LicenseFile)) {
        Write-Fail "FATAL: Themida license not found at $LicenseFile. Release aborted."
        Write-Host "  A -PrepareRelease build without Themida CANNOT be deployed." -ForegroundColor Red
        exit 1
    }

    # .tmd project: prefer the one in ThemidaDir, fallback to submodule
    $TmdProject = Get-ChildItem "$ThemidaDir\*.tmd" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $TmdProject) {
        $TmdProject = Get-ChildItem "$RepoRoot\themida\*.tmd" -ErrorAction SilentlyContinue | Select-Object -First 1
    }

    Write-Step "Applying Themida protection..."
    Write-Host "  Themida: $ThemidaExe" -ForegroundColor Gray
    $ProtectedLoader = "$RepoRoot\out\bin\Release\CobbleLoader_protected.exe"
    if (Test-Path $ProtectedLoader) { Remove-Item $ProtectedLoader -Force }
    if ($TmdProject) {
        Write-Host "  Project: $($TmdProject.FullName)" -ForegroundColor Gray
        & $ThemidaExe /protect_project $TmdProject.FullName /outputfile $ProtectedLoader /license $LicenseFile
    } else {
        Write-Host "  No .tmd project found, using direct CLI mode" -ForegroundColor Gray
        & $ThemidaExe /protect $LoaderOutput /outputfile $ProtectedLoader /license $LicenseFile
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $ProtectedLoader)) {
        Write-Fail "FATAL: Themida protection failed (exit=$LASTEXITCODE). Release aborted."
        Write-Host "  A -PrepareRelease build without Themida CANNOT be deployed." -ForegroundColor Red
        exit 1
    }
    # Replace the unprotected loader with the protected one. Manifest hashes
    # below will reflect the protected binary, so the loader's update flow
    # validates the same artifact users actually run.
    Move-Item $ProtectedLoader $LoaderOutput -Force
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
