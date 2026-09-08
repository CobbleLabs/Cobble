#Requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+(?:\.[0-9]+){2}(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?(?:\+[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$')]
    [ValidateLength(1, 32)]
    [string]$Version,
    [string]$BaseRef = "",
    [string]$OutputDirectory = "",
    [switch]$AllowDirty
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root "out\release-preview"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$categoryOrder = @("Movement", "Utility", "Network", "Combat", "Settings", "Gui")
$entries = [ordered]@{}
foreach ($category in $categoryOrder) {
    $entries[$category] = [ordered]@{}
}

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $output = & git -C $root @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed"
    }
    return @($output)
}

function Get-CodePointCount {
    param([string]$Value)
    $count = 0
    for ($index = 0; $index -lt $Value.Length; $index++) {
        if ([char]::IsHighSurrogate($Value[$index]) -and
            $index + 1 -lt $Value.Length -and
            [char]::IsLowSurrogate($Value[$index + 1])) {
            $index++
        }
        $count++
    }
    return $count
}

function Limit-UpdateText {
    param([string]$Value, [int]$Maximum)
    $value = ($Value -replace '[\x00-\x1F\x7F]', ' ').Trim()
    if ((Get-CodePointCount $value) -le $Maximum) {
        return $value
    }

    $target = $Maximum - 3
    $codePoints = 0
    $charLength = 0
    while ($charLength -lt $value.Length -and $codePoints -lt $target) {
        if ([char]::IsHighSurrogate($value[$charLength]) -and
            $charLength + 1 -lt $value.Length -and
            [char]::IsLowSurrogate($value[$charLength + 1])) {
            $charLength += 2
        }
        else {
            $charLength++
        }
        $codePoints++
    }
    return $value.Substring(0, $charLength).TrimEnd() + "..."
}

function Convert-ToDisplayName {
    param([string]$Value)
    $name = [IO.Path]::GetFileNameWithoutExtension($Value)
    $name = $name -replace '[_-]+', ' '
    $name = $name -creplace '([a-z0-9])([A-Z])', '$1 $2'
    if ([string]::IsNullOrWhiteSpace($name)) { return "Cobble Client" }
    return Limit-UpdateText (Get-Culture).TextInfo.ToTitleCase($name.ToLowerInvariant()) 64
}

function Get-PathDescriptor {
    param([string]$Path)
    $normalized = ($Path -replace '\\', '/').Trim()
    $lower = $normalized.ToLowerInvariant()

    if ($lower -match '^src/module/(movement|utility|combat|visual)/(.+)$') {
        $category = switch ($matches[1]) {
            "movement" { "Movement" }
            "combat" { "Combat" }
            "visual" { "Gui" }
            default { "Utility" }
        }
        return [pscustomobject]@{ Category = $category; Module = Convert-ToDisplayName $matches[2] }
    }
    if ($lower -match '^src/ui/(.+)$') {
        $segment = ($matches[1] -split '/')[0]
        return [pscustomobject]@{ Category = "Gui"; Module = Convert-ToDisplayName $segment }
    }
    if ($lower -match '(^|/)(settings?|config)(/|\.|$)' -or $lower -match 'serialization|preferences') {
        return [pscustomobject]@{ Category = "Settings"; Module = "Settings" }
    }
    if ($lower -match 'network|socket|protocol|request|websocket|heartbeat|ping|updater|server_config|jni') {
        return [pscustomobject]@{ Category = "Network"; Module = "Network" }
    }
    if ($lower -match '^loader/' -or $lower -match 'inject') {
        return [pscustomobject]@{ Category = "Utility"; Module = "Loader" }
    }
    if ($lower -match 'security|protection|integrity|vmprotect|reprotect') {
        return [pscustomobject]@{ Category = "Utility"; Module = "Security" }
    }
    if ($lower -match '^src/spectate/') {
        return [pscustomobject]@{ Category = "Utility"; Module = "Spectate" }
    }
    if ($lower -match '^src/hooks/') {
        return [pscustomobject]@{ Category = "Utility"; Module = "Hooks" }
    }
    return [pscustomobject]@{ Category = "Utility"; Module = "Cobble Client" }
}

function Normalize-Description {
    param([string]$Subject, [string]$Module)
    $description = $Subject.Trim()
    $description = $description -replace '^(feat|fix|perf|refactor|chore|build|docs|style|test)(\([^)]+\))?!?:\s*', ''
    if ([string]::IsNullOrWhiteSpace($description) -or $description -match '^(update|changes?|fix|wip)$') {
        $description = "Improved $Module."
    }
    return Limit-UpdateText $description 280
}

function Add-ReleaseEntry {
    param(
        [string]$Category,
        [string]$Module,
        [string]$Description,
        [string[]]$Details
    )
    $module = Limit-UpdateText $Module 64
    $description = Limit-UpdateText $Description 280
    $key = $module.ToLowerInvariant()
    if (-not $entries[$Category].Contains($key)) {
        $entries[$Category][$key] = [pscustomobject]@{
            module = $module
            description = $description
            details = New-Object 'System.Collections.Generic.List[string]'
            detailKeys = @{}
        }
    }
    $entry = $entries[$Category][$key]
    $descriptionKey = $description.ToLowerInvariant()
    if ($entry.description.ToLowerInvariant() -ne $descriptionKey -and
        -not $entry.detailKeys.ContainsKey($descriptionKey) -and
        $entry.details.Count -lt 12) {
        $entry.details.Add($description)
        $entry.detailKeys[$descriptionKey] = $true
    }
    foreach ($detail in $Details) {
        $clean = Limit-UpdateText (($detail -replace '^\s*[-*]\s*', '').Trim()) 240
        if (-not $clean -or $clean -match '^(Co-authored-by|Signed-off-by):') { continue }
        $detailKey = $clean.ToLowerInvariant()
        if (-not $entry.detailKeys.ContainsKey($detailKey) -and $entry.details.Count -lt 12) {
            $entry.details.Add($clean)
            $entry.detailKeys[$detailKey] = $true
        }
    }
}

function New-UpdateMarkdown {
    param([object[]]$Categories)
    $builder = New-Object System.Text.StringBuilder
    $rocket = ([string][char]0xD83D) + ([string][char]0xDE80)
    $warning = ([string][char]0x26A0) + ([string][char]0xFE0F)
    $heart = ([string][char]0x2764) + ([string][char]0xFE0F)
    [void]$builder.Append("# $rocket Cobble Update ($Version)`n`n")
    [void]$builder.Append("$warning *This update has been officially released and is now available to download from our website.*`n`n")
    foreach ($category in $Categories) {
        [void]$builder.Append("### $($category.name)`n`n")
        foreach ($entry in $category.entries) {
            [void]$builder.Append("* **$($entry.module):** $($entry.description)`n")
            if ($entry.details.Count -gt 0) {
                [void]$builder.Append("`n")
                foreach ($detail in $entry.details) {
                    [void]$builder.Append("  * $detail`n")
                }
            }
            [void]$builder.Append("`n")
        }
        if ($category.name -eq "Gui") {
            [void]$builder.Append("* General **performance optimizations, bug fixes, and improvements** across the client.`n`n")
        }
    }
    [void]$builder.Append("Thanks to everyone who tested the update and reported bugs. We appreciate all the feedback and hope you enjoy the new release! $heart`n`n@everyone")
    return $builder.ToString()
}

Push-Location $root
try {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "git is required to generate release notes"
    }
    Invoke-Git rev-parse --is-inside-work-tree | Out-Null

    $status = @(Invoke-Git status --porcelain=v1 --untracked-files=all)
    if ($status.Count -gt 0 -and -not $AllowDirty) {
        throw "The worktree is not clean. Commit the release changes first, or use -AllowDirty only for a reviewed emergency release."
    }

    if (-not $BaseRef) {
        $currentTag = "v$Version"
        try {
            $nearestTag = @(Invoke-Git describe --tags --abbrev=0 --match "v*" --exclude $currentTag HEAD)
            if ($nearestTag.Count -gt 0) {
                $BaseRef = [string]$nearestTag[0]
            }
        }
        catch {
            $BaseRef = ""
        }
    }
    if (-not $BaseRef) {
        throw "No previous production tag was found. Pass -BaseRef explicitly for this release."
    }
    Invoke-Git rev-parse --verify "$BaseRef^{commit}" | Out-Null

    $head = [string](Invoke-Git rev-parse HEAD | Select-Object -First 1)
    $commitLines = @(Invoke-Git log --no-merges --format="%H`t%s" "$BaseRef..HEAD")
    foreach ($line in $commitLines) {
        if (-not $line) { continue }
        $parts = $line -split "`t", 2
        if ($parts.Count -ne 2) { continue }
        $hash = $parts[0]
        $subject = $parts[1]
        $paths = @(Invoke-Git diff-tree --no-commit-id --name-only -r $hash)
        $body = @(Invoke-Git show -s --format=%b $hash)
        $descriptors = [ordered]@{}
        foreach ($path in $paths) {
            $descriptor = Get-PathDescriptor $path
            $descriptors["$($descriptor.Category)|$($descriptor.Module)"] = $descriptor
        }
        foreach ($descriptor in $descriptors.Values) {
            Add-ReleaseEntry $descriptor.Category $descriptor.Module (Normalize-Description $subject $descriptor.Module) $body
        }
    }

    if ($AllowDirty -and $status.Count -gt 0) {
        $ignoredPrefixes = @("out/", "generated/", "reprotect/", "vmprotect/", ".git/", "tools/", "docs/")
        $ignoredFiles = @("build.ps1", "cobble.sln", "cobble.vcxproj")
        $dirty = [ordered]@{}
        foreach ($line in $status) {
            if ($line.Length -lt 4) { continue }
            $code = $line.Substring(0, 2)
            $path = $line.Substring(3).Trim().Trim('"')
            if ($path -match ' -> ') { $path = ($path -split ' -> ')[-1].Trim('"') }
            $normalized = ($path -replace '\\', '/').ToLowerInvariant()
            if ($ignoredFiles -contains $normalized) { continue }
            if (@($ignoredPrefixes | Where-Object { $normalized.StartsWith($_) }).Count -gt 0) { continue }
            $descriptor = Get-PathDescriptor $path
            $key = "$($descriptor.Category)|$($descriptor.Module)"
            if (-not $dirty.Contains($key)) {
                $dirty[$key] = [pscustomobject]@{
                    Descriptor = $descriptor
                    Added = $false
                    Paths = New-Object 'System.Collections.Generic.List[string]'
                }
            }
            if ($code -match '\?|A') { $dirty[$key].Added = $true }
            if ($dirty[$key].Paths.Count -lt 6) { $dirty[$key].Paths.Add($path) }
        }
        foreach ($item in $dirty.Values) {
            $verb = if ($item.Added) { "Added" } else { "Improved" }
            $description = "$verb $($item.Descriptor.Module)."
            $details = @($item.Paths | ForEach-Object { "Changed $_" })
            Add-ReleaseEntry $item.Descriptor.Category $item.Descriptor.Module $description $details
        }
    }

    $categoryObjects = @()
    $seenModules = @{}
    $totalEntries = 0
    foreach ($category in $categoryOrder) {
        if ($entries[$category].Count -gt 25) {
            throw "Category $category has more than 25 entries. Consolidate related changes before release."
        }
        $entryObjects = @()
        foreach ($entry in $entries[$category].Values) {
            $moduleKey = $entry.module.ToLowerInvariant()
            if ($seenModules.ContainsKey($moduleKey)) {
                throw "Module $($entry.module) appears in more than one category. Use distinct module names before release."
            }
            $seenModules[$moduleKey] = $true
            $totalEntries++
            if ($totalEntries -gt 50) {
                throw "Generated release notes contain more than 50 entries. Consolidate related changes before release."
            }
            $entryObjects += [ordered]@{
                module = $entry.module
                description = $entry.description
                details = @($entry.details)
            }
        }
        if ($entryObjects.Count -gt 0) {
            $categoryObjects += [ordered]@{ name = $category; entries = $entryObjects }
        }
    }
    if ($categoryObjects.Count -eq 0) {
        throw "No user-facing release changes were found between $BaseRef and HEAD."
    }

    $markdown = New-UpdateMarkdown $categoryObjects
    $markdownLength = Get-CodePointCount $markdown
    if ($markdownLength -gt 2000) {
        throw "Generated announcement is $markdownLength characters; Discord allows 2000. Improve or squash commit notes before release."
    }

    $payload = [ordered]@{ version = $Version; pinned = $true; categories = $categoryObjects }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $jsonPath = Join-Path $OutputDirectory "$Version.json"
    $markdownPath = Join-Path $OutputDirectory "$Version.md"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($jsonPath, ($payload | ConvertTo-Json -Depth 8), $utf8NoBom)
    [IO.File]::WriteAllText($markdownPath, $markdown, $utf8NoBom)

    Write-Host "Release notes: $BaseRef -> $($head.Substring(0, 8))" -ForegroundColor Cyan
    Write-Host "JSON: $jsonPath" -ForegroundColor Green
    Write-Host "Preview: $markdownPath ($markdownLength/2000)" -ForegroundColor Green
    Write-Output $jsonPath
}
finally {
    Pop-Location
}
