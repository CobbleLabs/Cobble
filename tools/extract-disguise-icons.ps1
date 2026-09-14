#Requires -Version 5.1
<#
.SYNOPSIS
    Extract disguise icons from programs installed on THIS pc.

.DESCRIPTION
    Pulls the 256px icon out of each installed program and saves it as
    icons/<slug>.ico, ready for build.ps1 -Pack (New-DisguiseVariants).
    Programs that are not installed are SKIPPED - those slugs fall back
    to the stock binary server-side. You can also drop any .ico file
    manually as icons/<slug>.ico. Run once per build pc; icons persist
    for future releases:
        .\tools\extract-disguise-icons.ps1

.EXAMPLE
    .\tools\extract-disguise-icons.ps1 -Slugs spotify,discord,steam -Force
#>
param(
    [string]$IconsDir = (Join-Path $PSScriptRoot "..\icons"),
    [string[]]$Slugs = @(
        "anydesk","obs-studio","spotify","discord","lunar-client","steam",
        "notion","slack","figma","vscode","chrome","firefox",
        "zoom","teams","skype","blender","unity-hub","epic-games",
        "overwolf","razer-synapse","nvidia-geforce-experience","amd-adrenalin","intel-driver-support","realtek-audio",
        "corsair-icue","steelseries-gg","logitech-ghub","windows-update","microsoft-store","valorant",
        "adobe-photoshop","adobe-premiere","autocad","sketchup","visual-studio","web-browser",
        "system-utility","security-update","microsoft-edge","driver-update","riot-client"
    ),
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class DisguiseIconExtractor {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint PrivateExtractIcons(string lpszFile, int nIconIndex, int cxIcon, int cyIcon, IntPtr[] phicon, uint[] piconid, uint nIcons, uint flags);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool DestroyIcon(IntPtr hIcon);
}
"@

$Sources = [ordered]@{
    "anydesk" = @("${env:ProgramFiles(x86)}\AnyDesk\AnyDesk.exe"; "$env:ProgramFiles\AnyDesk\AnyDesk.exe");
    "obs-studio" = @("$env:ProgramFiles\obs-studio\bin\64bit\obs64.exe");
    "spotify" = @("$env:APPDATA\Spotify\Spotify.exe");
    "discord" = @("$env:LOCALAPPDATA\Discord\Update.exe");
    "lunar-client" = @("$env:LOCALAPPDATA\Programs\lunarclient\Lunar Client.exe"; "$env:USERPROFILE\.lunarclient\launcher.exe");
    "steam" = @("${env:ProgramFiles(x86)}\Steam\steam.exe");
    "notion" = @("$env:LOCALAPPDATA\Programs\Notion\Notion.exe");
    "slack" = @("$env:LOCALAPPDATA\slack\slack.exe");
    "figma" = @("$env:LOCALAPPDATA\Figma\app-*\Figma.exe");
    "vscode" = @("$env:LOCALAPPDATA\Programs\Microsoft VS Code\Code.exe");
    "chrome" = @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe"; "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe");
    "firefox" = @("$env:ProgramFiles\Mozilla Firefox\firefox.exe"; "${env:ProgramFiles(x86)}\Mozilla Firefox\firefox.exe");
    "zoom" = @("$env:APPDATA\Zoom\bin\Zoom.exe");
    "teams" = @("$env:LOCALAPPDATA\Microsoft\Teams\Update.exe");
    "skype" = @("${env:ProgramFiles(x86)}\Microsoft\Skype for Desktop\Skype.exe");
    "blender" = @("$env:ProgramFiles\Blender Foundation\Blender*\blender.exe");
    "unity-hub" = @("$env:ProgramFiles\Unity Hub\Unity Hub.exe");
    "epic-games" = @("${env:ProgramFiles(x86)}\Epic Games\Launcher\Portal\Binaries\Win32\EpicGamesLauncher.exe");
    "overwolf" = @("${env:ProgramFiles(x86)}\Overwolf\OverwolfLauncher.exe"; "${env:ProgramFiles(x86)}\Overwolf\Overwolf.exe");
    "razer-synapse" = @("${env:ProgramFiles(x86)}\Razer\Synapse3\WPFUI\Framework\Razer Synapse 3 Host\Razer Synapse 3.exe");
    "nvidia-geforce-experience" = @("$env:ProgramFiles\NVIDIA Corporation\NVIDIA GeForce Experience\NVIDIA GeForce Experience.exe");
    "amd-adrenalin" = @("$env:ProgramFiles\AMD\CNext\CNext\RadeonSoftware.exe");
    "intel-driver-support" = @("$env:ProgramFiles\Intel\Driver and Support Assistant\DSATray.exe");
    "realtek-audio" = @("$env:ProgramFiles\Realtek\Audio\HDA\RtkNGUI64.exe");
    "corsair-icue" = @("$env:ProgramFiles\Corsair\CORSAIR iCUE 5 software\iCUE.exe"; "$env:ProgramFiles\Corsair\CORSAIR iCUE software\iCUE.exe");
    "steelseries-gg" = @("$env:ProgramFiles\SteelSeries\GG\SteelSeriesGG.exe");
    "logitech-ghub" = @("$env:ProgramFiles\LGHUB\lghub.exe");
    "windows-update" = @("$env:SystemRoot\System32\wuauclt.exe");
    "microsoft-store" = @("$env:SystemRoot\System32\WinStore.App.exe");
    "valorant" = @("C:\Riot Games\VALORANT\live\VALORANT.exe"; "${env:ProgramFiles}\Riot Games\VALORANT\live\VALORANT.exe");
    "adobe-photoshop" = @("$env:ProgramFiles\Adobe\Adobe Photoshop 2*\Photoshop.exe");
    "adobe-premiere" = @("$env:ProgramFiles\Adobe\Adobe Premiere Pro 2*\Adobe Premiere Pro.exe");
    "autocad" = @("$env:ProgramFiles\Autodesk\AutoCAD 2*\acad.exe");
    "sketchup" = @("$env:ProgramFiles\SketchUp\SketchUp 2*\SketchUp.exe");
    "visual-studio" = @("$env:ProgramFiles\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"; "$env:ProgramFiles\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe");
    "web-browser" = @("$env:ProgramFiles\Internet Explorer\iexplore.exe"; "${env:ProgramFiles(x86)}\Internet Explorer\iexplore.exe");
    "system-utility" = @("$env:SystemRoot\System32\msconfig.exe");
    "security-update" = @("$env:SystemRoot\System32\wscui.cpl");
    "microsoft-edge" = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"; "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe");
    "driver-update" = @("$env:SystemRoot\System32\hdwwiz.exe");
    "riot-client" = @("C:\Riot Games\Riot Client\RiotClientServices.exe"; "${env:ProgramFiles}\Riot Games\Riot Client\RiotClientServices.exe");
}

function Save-ExeIcon {
    param([string]$ExePath, [string]$IcoPath)
    $hicons = New-Object IntPtr[] 1
    $ids = New-Object uint[] 1
    $n = [DisguiseIconExtractor]::PrivateExtractIcons($ExePath, 0, 256, 256, $hicons, $ids, 1, 0)
    if ($n -eq 0 -or $hicons[0] -eq [IntPtr]::Zero) { return $false }
    try {
        $bmp = [System.Drawing.Bitmap]::FromHicon($hicons[0])
        try {
            $ms = New-Object System.IO.MemoryStream
            $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
            $png = $ms.ToArray()
            $ms.Close()
            $fs = [System.IO.File]::Create($IcoPath)
            try {
                $bw = New-Object System.IO.BinaryWriter($fs)
                $bw.Write([uint16]0)
                $bw.Write([uint16]1)
                $bw.Write([uint16]1)
                $bw.Write([byte]0)
                $bw.Write([byte]0)
                $bw.Write([byte]0)
                $bw.Write([byte]0)
                $bw.Write([uint16]1)
                $bw.Write([uint16]32)
                $bw.Write([uint32]$png.Length)
                $bw.Write([uint32]22)
                $bw.Write($png)
                $bw.Flush()
            } finally { $bw.Close() }
            return $true
        } finally { $bmp.Dispose() }
    } finally { [void][DisguiseIconExtractor]::DestroyIcon($hicons[0]) }
}

New-Item -ItemType Directory -Force -Path $IconsDir | Out-Null
$ok = 0
$skipped = 0
foreach ($slug in $Slugs) {
    $dest = Join-Path $IconsDir ($slug + ".ico")
    if ((Test-Path $dest) -and (-not $Force)) { Write-Host "KEEP  $slug (exists, use -Force to redo)"; continue }
    $found = $null
    foreach ($pat in $Sources[$slug]) {
        if (-not $pat) { continue }
        $m = @(Resolve-Path $pat -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($m.Count -gt 0 -and (Test-Path $m[0].Path -PathType Leaf)) { $found = $m[0].Path; break }
    }
    if (-not $found) { Write-Host "SKIP  $slug (program not installed)"; $skipped++; continue }
    if (Save-ExeIcon -ExePath $found -IcoPath $dest) { Write-Host "OK    $slug <- $found"; $ok++ }
    else { Write-Host "SKIP  $slug (no extractable icon)"; $skipped++ }
}
Write-Host ""
Write-Host "Icons ready: $ok, skipped: $skipped. Next: .\build.ps1 -Pack -Deploy -Mandatory"
