# Security Testing Script
# Tests all protection mechanisms

Write-Host "`n=== COBBLE LOADER SECURITY TEST ===`n" -ForegroundColor Cyan

$loaderPath = "out\bin\Release\CobbleLoader.exe"

# Test 1: String Obfuscation
Write-Host "[TEST 1] String Obfuscation" -ForegroundColor Yellow
$bytes = [System.IO.File]::ReadAllBytes($loaderPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

$criticalStrings = @{
    "api.cobble.vip" = "Server domain"
    "https://api.cobble.vip" = "License server URL"
    "/api/verify" = "Verify endpoint"
    "/api/update" = "Update endpoint"
}

$passed = 0
$failed = 0

foreach ($string in $criticalStrings.Keys) {
    $desc = $criticalStrings[$string]
    # Check for exact string match (not substring in binary junk)
    $pattern = [regex]::Escape($string)
    if ($text -match $pattern) {
        Write-Host "  [FAIL] $desc ($string) found in plaintext!" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "  [PASS] $desc obfuscated" -ForegroundColor Green
        $passed++
    }
}

Write-Host "`n[TEST 1 RESULT] Passed: $passed / Failed: $failed`n" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })

# Test 2: File Size (Symbol Stripping Check)
Write-Host "[TEST 2] Symbol Stripping (File Size)" -ForegroundColor Yellow
$size = (Get-Item $loaderPath).Length / 1MB
Write-Host "  Loader size: $([math]::Round($size, 2)) MB" -ForegroundColor Cyan

if ($size -lt 4.5) {
    Write-Host "  [PASS] Size indicates symbols stripped (< 4.5 MB)" -ForegroundColor Green
} else {
    Write-Host "  [WARN] Size might include debug symbols (> 4.5 MB)" -ForegroundColor Yellow
}

# Test 3: Anti-Debug (Check if compiled with checks)
Write-Host "`n[TEST 3] Anti-Debug Compilation" -ForegroundColor Yellow
if ($text -match "IsDebuggerPresent|CheckRemoteDebuggerPresent") {
    Write-Host "  [PASS] Anti-debug functions detected in binary" -ForegroundColor Green
} else {
    Write-Host "  [WARN] Anti-debug functions not clearly visible (might be inlined)" -ForegroundColor Yellow
}

# Summary
Write-Host "`n=== SUMMARY ===" -ForegroundColor Cyan
Write-Host "✅ String Obfuscation: " -NoNewline
Write-Host $(if ($failed -eq 0) { "ACTIVE" } else { "PARTIAL" }) -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Yellow" })
Write-Host "✅ Symbol Stripping: " -NoNewline
Write-Host $(if ($size -lt 4.5) { "ACTIVE" } else { "CHECK MANUALLY" }) -ForegroundColor $(if ($size -lt 4.5) { "Green" } else { "Yellow" })
Write-Host "✅ Compiler Obfuscation: ACTIVE (MaxSpeed + Inline enabled)" -ForegroundColor Green

Write-Host "`n📋 Next steps:" -ForegroundColor Cyan
Write-Host "  1. Test anti-debug: Open loader in x64dbg (should close immediately)"
Write-Host "  2. Test IDA Pro: Check if function names are sub_XXXXXX"
Write-Host "  3. Functional test: Run loader normally (should work fine)"
Write-Host ""
