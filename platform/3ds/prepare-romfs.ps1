param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"

$platformDir = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $platformDir "..\..")).Path
$romfs = Join-Path $platformDir "romfs"
$resolvedParent = (Resolve-Path -LiteralPath (Split-Path -Parent $romfs)).Path

if ($resolvedParent -ne $platformDir) {
    throw "Refusing to prepare RomFS outside $platformDir"
}

if (Test-Path -LiteralPath $romfs) {
    Remove-Item -LiteralPath $romfs -Recurse -Force
}

New-Item -ItemType Directory -Path $romfs | Out-Null

function Copy-RuntimeFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination,
        [Parameter(Mandatory = $true)]
        [string[]]$Extensions
    )

    $sourcePath = Join-Path $sourceRoot $Source
    $destinationPath = Join-Path $romfs $Destination

    Get-ChildItem -LiteralPath $sourcePath -Recurse -File |
        Where-Object { $Extensions -contains $_.Extension.ToLowerInvariant() } |
        ForEach-Object {
            $relative = $_.FullName.Substring($sourcePath.Length).TrimStart("\", "/")
            $target = Join-Path $destinationPath $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $target
        }
}

Copy-RuntimeFiles -Source "config" -Destination "config" -Extensions @(".cfg", ".srv")
Copy-RuntimeFiles -Source "language" -Destination "language" -Extensions @(".txt")
Copy-RuntimeFiles -Source "textures" -Destination "textures" -Extensions @(".png", ".jpg", ".ttf", ".cfg", ".txt")
Copy-RuntimeFiles -Source "models" -Destination "models" -Extensions @(".mod")
Copy-RuntimeFiles -Source "sound" -Destination "sound" -Extensions @(".ogg", ".wav")
Copy-RuntimeFiles -Source "music" -Destination "music" -Extensions @(".ogg", ".m3u", ".aatrack")

$aiTemplate = Join-Path $sourceRoot "config\aiplayers.cfg.in"
$rcTemplate = Join-Path $sourceRoot "config\rc.config.in"
Copy-Item -LiteralPath $aiTemplate -Destination (Join-Path $romfs "config\aiplayers.cfg")
Copy-Item -LiteralPath $rcTemplate -Destination (Join-Path $romfs "config\rc.config")

$languagesTemplate = Get-Content -LiteralPath (Join-Path $sourceRoot "language\languages.txt.in") -Raw
$languages = $languagesTemplate.Replace("@progtitle@", "Armagetron Advanced")
$languageIncludes = @(
    "include british.txt"
    "include american.txt"
    "include spanish.txt"
    "include russian.txt"
    "include polish.txt"
    "include galician.txt"
    "include french.txt"
    "include deutsch.txt"
    # Last, so the 3DS control wording replaces the desktop wording.
    "include 3ds.txt"
) -join "`n"
$languages = [Text.RegularExpressions.Regex]::Replace(
    $languages,
    "(?m)^include .+\.txt\r?\n?",
    "")
$languages = $languages.TrimEnd() + "`n`n" + $languageIncludes + "`n"
[IO.File]::WriteAllText(
    (Join-Path $romfs "language\languages.txt"),
    $languages,
    (New-Object Text.UTF8Encoding($false)))

$includedResources = Join-Path $romfs "resource\included"
& $Python (Join-Path $sourceRoot "batch\make\copyresources.py") `
    (Join-Path $sourceRoot "resource\proto") `
    $includedResources
if ($LASTEXITCODE -ne 0) {
    throw "Resource preparation failed with exit code $LASTEXITCODE"
}

$binaryResources = Join-Path $sourceRoot "resource\binary"
if (Test-Path -LiteralPath $binaryResources) {
    Copy-RuntimeFiles `
        -Source "resource\binary" `
        -Destination "resource\included" `
        -Extensions @(".png")
}

$fileCount = (Get-ChildItem -LiteralPath $romfs -Recurse -File).Count
$byteCount = (Get-ChildItem -LiteralPath $romfs -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
Write-Host "Prepared RomFS: $fileCount files, $byteCount bytes"
