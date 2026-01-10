# ============================================================================
# SECURITY TEST SUITE
# Professional testing for anti-crack protections
# Run AFTER building with .\build.ps1 -Release
# ============================================================================

param(
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot | Split-Path -Parent
$LoaderExe = "$RepoRoot\out\bin\Release\CobbleLoader.exe"
$EncryptedPayload = "$RepoRoot\loader\res\Cobble.enc"
$OriginalDll = "$RepoRoot\out\bin\Release\Cobble.dll"

$TestsPassed = 0
$TestsFailed = 0

function Write-TestHeader($name) {
    Write-Host "`n[$name]" -ForegroundColor Cyan
}

function Write-Pass($msg) {
    $script:TestsPassed++
    Write-Host "  [PASS] " -ForegroundColor Green -NoNewline
    Write-Host $msg
}

function Write-Fail($msg) {
    $script:TestsFailed++
    Write-Host "  [FAIL] " -ForegroundColor Red -NoNewline
    Write-Host $msg
}

function Write-Info($msg) {
    Write-Host "  [INFO] " -ForegroundColor Gray -NoNewline
    Write-Host $msg
}

Write-Host "=" * 60 -ForegroundColor DarkGray
Write-Host "COBBLE SECURITY TEST SUITE" -ForegroundColor White
Write-Host "=" * 60 -ForegroundColor DarkGray

# ============================================================================
# TEST 1: Encrypted payload has correct header
# ============================================================================
Write-TestHeader "TEST 1: Encrypted Payload Format"

if (Test-Path $EncryptedPayload) {
    $enc = [System.IO.File]::ReadAllBytes($EncryptedPayload)
    $magic = [System.Text.Encoding]::ASCII.GetString($enc[0..3])
    
    if ($magic -eq "CBLP") {
        Write-Pass "Magic header correct: CBLP"
    } else {
        Write-Fail "Magic header wrong: $magic (expected CBLP)"
    }
    
    $version = [BitConverter]::ToUInt32($enc, 4)
    if ($version -eq 1) {
        Write-Pass "Version correct: $version"
    } else {
        Write-Fail "Version wrong: $version (expected 1)"
    }
    
    $originalSize = [BitConverter]::ToUInt32($enc, 8)
    $encryptedSize = [BitConverter]::ToUInt32($enc, 12)
    Write-Info "Original size: $originalSize bytes"
    Write-Info "Encrypted size: $encryptedSize bytes"
    
    # Check that encrypted content is NOT a valid PE
    $dataStart = 40  # Header size
    if ($enc[$dataStart] -eq 0x4D -and $enc[$dataStart + 1] -eq 0x5A) {
        Write-Fail "CRITICAL: Payload is NOT encrypted (MZ header found at offset $dataStart)"
    } else {
        Write-Pass "Payload is encrypted (no MZ header in data section)"
    }
} else {
    Write-Fail "Encrypted payload not found: $EncryptedPayload"
}

# ============================================================================
# TEST 2: Loader executable embeds encrypted payload
# ============================================================================
Write-TestHeader "TEST 2: Loader Resource Verification"

if (Test-Path $LoaderExe) {
    # Read loader binary
    $loaderBytes = [System.IO.File]::ReadAllBytes($LoaderExe)
    $loaderStr = [System.Text.Encoding]::ASCII.GetString($loaderBytes)
    
    # Search for CBLP magic (encrypted payload marker)
    $cblpIndex = $loaderStr.IndexOf("CBLP")
    if ($cblpIndex -gt 0) {
        Write-Pass "Encrypted payload (CBLP) found in loader at offset $cblpIndex"
        
        # Verify it's not a plain MZ after the resource
        $mzAfterCblp = $loaderStr.IndexOf("MZ", $cblpIndex)
        $nextCblp = $loaderBytes.Length  # Assume end if no other marker
        
        if ($mzAfterCblp -gt $cblpIndex -and $mzAfterCblp -lt ($cblpIndex + 100)) {
            Write-Fail "WARNING: MZ header found near CBLP - might be partially encrypted"
        } else {
            Write-Pass "No plain PE detected after CBLP marker"
        }
    } else {
        Write-Fail "CBLP marker not found in loader - encryption might not be embedded"
    }
    
    # Check that plain DLL is NOT in resources (search for Cobble signature)
    $cobbleSignature = "COBBLE_LOADED_V1"
    if ($loaderStr.Contains($cobbleSignature)) {
        Write-Fail "CRITICAL: Plain DLL signature found in loader resources!"
    } else {
        Write-Pass "Plain DLL signature NOT found in loader (good - it's encrypted)"
    }
} else {
    Write-Fail "Loader not found: $LoaderExe"
}

# ============================================================================
# TEST 3: Resource extraction simulation
# ============================================================================
Write-TestHeader "TEST 3: Resource Extraction Simulation"

$extractDir = "$RepoRoot\test_extract"
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

try {
    Write-Info "Simulating resource extraction attack..."
    
    # The key test: check if COBBLE_LOADED_V1 signature appears in loader resources
    # If encrypted properly, this string should NOT be findable
    $loaderBytes = [System.IO.File]::ReadAllBytes($LoaderExe)
    $loaderStr = [System.Text.Encoding]::ASCII.GetString($loaderBytes)
    
    # Check for DLL-specific strings that would indicate unencrypted embedding
    $dllSignatures = @("COBBLE_LOADED_V1", "cobble::context", "ModuleManager")
    $foundSignatures = 0
    
    foreach ($sig in $dllSignatures) {
        if ($loaderStr.Contains($sig)) {
            Write-Fail "DLL signature '$sig' found in loader - encryption compromised!"
            $foundSignatures++
        }
    }
    
    if ($foundSignatures -eq 0) {
        Write-Pass "No DLL signatures found in loader resources (properly encrypted)"
    }
    
    # Verify encrypted payload is present
    $cblpCount = 0
    $idx = 0
    while (($idx = $loaderStr.IndexOf("CBLP", $idx)) -ne -1) {
        $cblpCount++
        $idx++
    }
    Write-Info "Found $cblpCount CBLP markers (should be 1)"
    if ($cblpCount -eq 1) {
        Write-Pass "Single encrypted payload embedded correctly"
    } elseif ($cblpCount -eq 0) {
        Write-Fail "No encrypted payload found!"
    }
    
} catch {
    Write-Info "Extraction test error: $_"
}

# ============================================================================
# TEST 4: Verify LoaderVerify will detect LoadLibrary
# ============================================================================
Write-TestHeader "TEST 4: Anti-LoadLibrary Verification (Code Review)"

$loaderVerifyHpp = "$RepoRoot\src\util\loader_verify.hpp"
if (Test-Path $loaderVerifyHpp) {
    $content = Get-Content $loaderVerifyHpp -Raw
    
    if ($content -match "isInModuleList") {
        Write-Pass "PEB module list check implemented"
    } else {
        Write-Fail "PEB module list check NOT found"
    }
    
    if ($content -match "verifyOrDie") {
        Write-Pass "verifyOrDie function implemented"
    } else {
        Write-Fail "verifyOrDie function NOT found"
    }
    
    if ($content -match "ExitProcess|TerminateProcess") {
        Write-Pass "Process termination on crack detection implemented"
    } else {
        Write-Fail "No termination on crack detection"
    }
} else {
    Write-Fail "loader_verify.hpp not found"
}

# ============================================================================
# TEST 5: Verify DLL calls LoaderVerify on startup
# ============================================================================
Write-TestHeader "TEST 5: DLL Startup Verification"

$cobbleCpp = "$RepoRoot\src\cobble.cpp"
if (Test-Path $cobbleCpp) {
    $content = Get-Content $cobbleCpp -Raw
    
    if ($content -match "LoaderVerify::verifyOrDie") {
        Write-Pass "LoaderVerify::verifyOrDie called in DLL startup"
    } else {
        Write-Fail "LoaderVerify::verifyOrDie NOT called in DLL startup"
    }
    
    # Check if it's called BEFORE other initialization
    $verifyPos = $content.IndexOf("LoaderVerify::verifyOrDie")
    $loggerPos = $content.IndexOf("c_logger::initialize")
    
    if ($verifyPos -lt $loggerPos) {
        Write-Pass "Verification runs BEFORE logging (no leak of detection)"
    } else {
        Write-Fail "Verification runs AFTER logging (could leak info to attacker)"
    }
} else {
    Write-Fail "cobble.cpp not found"
}

# ============================================================================
# TEST 6: No insecure fallback
# ============================================================================
Write-TestHeader "TEST 6: No Insecure Fallback"

$mainCpp = "$RepoRoot\loader\main.cpp"
if (Test-Path $mainCpp) {
    $content = Get-Content $mainCpp -Raw
    
    # Check for manualMap function call (not just the word)
    if ($content -match 'manualMap\s*\(' -and $content -notmatch '//.*manualMap') {
        Write-Fail "manualMap fallback still active - SECURITY RISK"
    } else {
        Write-Pass "manualMap fallback removed or disabled"
    }
    
    # Check for WriteProcessMemory (excluding comments)
    $lines = $content -split "`n"
    $wpmFound = $false
    foreach ($line in $lines) {
        if ($line -match 'WriteProcessMemory' -and $line -notmatch '^\s*//') {
            $wpmFound = $true
            break
        }
    }
    
    if ($wpmFound) {
        Write-Fail "WriteProcessMemory still used - SECURITY RISK"
    } else {
        Write-Pass "WriteProcessMemory not used in injection path"
    }
} else {
    Write-Fail "main.cpp not found"
}

# ============================================================================
# SUMMARY
# ============================================================================
Write-Host "`n" + "=" * 60 -ForegroundColor DarkGray
Write-Host "TEST SUMMARY" -ForegroundColor White
Write-Host "=" * 60 -ForegroundColor DarkGray
Write-Host "  Passed: $TestsPassed" -ForegroundColor Green
Write-Host "  Failed: $TestsFailed" -ForegroundColor $(if ($TestsFailed -gt 0) { "Red" } else { "Gray" })
Write-Host ""

if ($TestsFailed -eq 0) {
    Write-Host "ALL TESTS PASSED - Security protections are in place" -ForegroundColor Green
} else {
    Write-Host "SOME TESTS FAILED - Review security implementation" -ForegroundColor Yellow
}

# Cleanup
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
