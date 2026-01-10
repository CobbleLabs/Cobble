# ============================================================================
# ENCRYPT DLL PAYLOAD
# Encrypts Cobble.dll with AES-256-CBC before embedding in loader
# Server key REQUIRED - no legacy fallback
#
# Usage:
#   .\encrypt_payload.ps1 -InputDll Cobble.dll -PayloadKey $env:PAYLOAD_KEY
# ============================================================================

param(
    [Parameter(Mandatory=$true)]
    [string]$InputDll,
    
    [Parameter(Mandatory=$true)]
    [string]$PayloadKey,  # 64 hex chars - REQUIRED
    
    [Parameter(Mandatory=$false)]
    [string]$OutputFile = "",
    
    [switch]$Quiet
)

function Parse-HexKey {
    param([string]$HexKey)
    if ($HexKey.Length -ne 64) {
        return $null
    }
    $bytes = New-Object byte[] 32
    for ($i = 0; $i -lt 32; $i++) {
        $bytes[$i] = [Convert]::ToByte($HexKey.Substring($i * 2, 2), 16)
    }
    return $bytes
}

function Encrypt-Payload {
    param(
        [byte[]]$Data,
        [byte[]]$Key
    )
    
    # Generate random IV
    $iv = New-Object byte[] 16
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($iv)
    
    # Create AES encryptor
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.KeySize = 256
    $aes.BlockSize = 128
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
    $aes.Key = $Key
    $aes.IV = $iv
    
    # Encrypt
    $encryptor = $aes.CreateEncryptor()
    $ms = New-Object System.IO.MemoryStream
    $cs = New-Object System.Security.Cryptography.CryptoStream($ms, $encryptor, [System.Security.Cryptography.CryptoStreamMode]::Write)
    $cs.Write($Data, 0, $Data.Length)
    $cs.FlushFinalBlock()
    $encrypted = $ms.ToArray()
    
    $cs.Close()
    $ms.Close()
    $aes.Dispose()
    
    return @{
        Encrypted = $encrypted
        IV = $iv
    }
}

# Validate input
if (-not (Test-Path $InputDll)) {
    Write-Host "Error: Input DLL not found: $InputDll" -ForegroundColor Red
    exit 1
}

# Parse and validate key - REQUIRED
$key = Parse-HexKey -HexKey $PayloadKey
if ($null -eq $key) {
    Write-Host "Error: PayloadKey must be exactly 64 hex characters (32 bytes)" -ForegroundColor Red
    Write-Host "Generate with: openssl rand -hex 32" -ForegroundColor Yellow
    exit 1
}

# Read DLL
$dllData = [System.IO.File]::ReadAllBytes($InputDll)
$originalSize = $dllData.Length

if (-not $Quiet) {
    Write-Host "Input DLL: $InputDll" -ForegroundColor Cyan
    Write-Host "Original size: $originalSize bytes" -ForegroundColor Cyan
    Write-Host "Key source: SERVER KEY (secure)" -ForegroundColor Green
}

# Validate PE
if ($dllData[0] -ne 0x4D -or $dllData[1] -ne 0x5A) {
    Write-Host "Error: Invalid PE file (no MZ header)" -ForegroundColor Red
    exit 1
}

# Encrypt
$result = Encrypt-Payload -Data $dllData -Key $key

# Build header (40 bytes total) - must match C++ struct exactly
# magic[4] + version[4] + originalSize[4] + encryptedSize[4] + iv[16] + reserved[8] = 40 bytes
$header = New-Object byte[] 40

# Magic: "CBLP" (offset 0-3)
$header[0] = 0x43  # C
$header[1] = 0x42  # B
$header[2] = 0x4C  # L
$header[3] = 0x50  # P

# Version: 1 (offset 4-7, little endian)
$header[4] = 1
$header[5] = 0
$header[6] = 0
$header[7] = 0

# Original size (offset 8-11, little endian)
$sizeBytes = [BitConverter]::GetBytes([uint32]$originalSize)
[Array]::Copy($sizeBytes, 0, $header, 8, 4)

# Encrypted size (offset 12-15, little endian)
$encSizeBytes = [BitConverter]::GetBytes([uint32]$result.Encrypted.Length)
[Array]::Copy($encSizeBytes, 0, $header, 12, 4)

# IV (offset 16-31)
[Array]::Copy($result.IV, 0, $header, 16, 16)

# Reserved (offset 32-39) - already zero

# Combine header + encrypted data
$output = New-Object byte[] ($header.Length + $result.Encrypted.Length)
[Array]::Copy($header, 0, $output, 0, $header.Length)
[Array]::Copy($result.Encrypted, 0, $output, $header.Length, $result.Encrypted.Length)

# Determine output path
if ([string]::IsNullOrEmpty($OutputFile)) {
    $OutputFile = [System.IO.Path]::ChangeExtension($InputDll, ".enc")
}

# Write output
[System.IO.File]::WriteAllBytes($OutputFile, $output)

if (-not $Quiet) {
    Write-Host ""
    Write-Host "SUCCESS!" -ForegroundColor Green
    Write-Host "Output: $OutputFile" -ForegroundColor Green
    Write-Host "Total size: $($output.Length) bytes" -ForegroundColor Green
}

# Clear sensitive data
[Array]::Clear($key, 0, $key.Length)
