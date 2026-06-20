$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Build Tools were not found."
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe |
    Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild was not found."
}

& $msbuild (Join-Path $root "preview\CobbleGuiPreview.vcxproj") /m /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Preview: $(Join-Path $root 'out\bin\Dev\CobbleGuiPreview.exe')"
