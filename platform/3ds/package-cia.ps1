param(
    [string]$Makerom = 'D:\PsyDoom-Project\LEGOWB3DS\tools\makerom.exe',
    [string]$Bannertool = 'D:\PsyDoom-Project\LEGOWB3DS\tools\bannertool.exe',
    [string]$UniqueId = '0xF4A4D'
)

$ErrorActionPreference = 'Stop'

$project = $PSScriptRoot
$elf = Join-Path $project 'armagetronad-3ds.elf'
$smdh = Join-Path $project 'armagetronad-3ds.smdh'
$romfs = Join-Path $project 'romfs'
$rsf = Join-Path $project 'armagetronad-3ds.rsf'
$output = Join-Path $project 'armagetronad-3ds.cia'
$packageBuild = Join-Path $project 'build-full\cia'
$bannerPng = Join-Path $packageBuild 'banner.png'
$bannerWav = Join-Path $project 'banner.wav'
$banner = Join-Path $packageBuild 'banner.bnr'
$icon = Join-Path $project '..\..\desktop\icons\128x128\armagetronad.png'

foreach ($required in @($Makerom, $Bannertool, $elf, $smdh, $romfs, $rsf, $icon, $bannerWav)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required CIA input was not found: $required"
    }
}

if ($UniqueId -notmatch '^0x[0-9A-Fa-f]{5}$') {
    throw 'UniqueId must be a five-digit hexadecimal 3DS application ID, for example 0xF4A4D.'
}

New-Item -ItemType Directory -Force -Path $packageBuild | Out-Null

Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Image]::FromFile($icon)
$bitmap = New-Object System.Drawing.Bitmap 256, 128
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.Clear([System.Drawing.Color]::FromArgb(8, 8, 18))
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($source, 64, 0, 128, 128)
    $bitmap.Save($bannerPng, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
    $source.Dispose()
}

# The HOME menu wants sixteen bit stereo at 16364 Hz and at most three
# seconds, and bannertool converts whatever it is handed without complaint.
# This used to build a mono second of silence at twice the rate, which the menu
# read as noise rather than as nothing. Check the file rather than trust it:
# getting this wrong is not a build error, it is a sound the console makes at
# whoever is holding it.
$bannerFormat = [System.IO.File]::ReadAllBytes($bannerWav)
$bannerChannels = [System.BitConverter]::ToInt16($bannerFormat, 22)
$bannerRate = [System.BitConverter]::ToInt32($bannerFormat, 24)
$bannerBits = [System.BitConverter]::ToInt16($bannerFormat, 34)
$bannerFrames = ([System.BitConverter]::ToInt32($bannerFormat, 40)) / 4
if ($bannerChannels -ne 2 -or $bannerRate -ne 16364 -or $bannerBits -ne 16) {
    throw "banner.wav must be 16 bit stereo at 16364 Hz, found $bannerBits bit, $bannerChannels channel, $bannerRate Hz."
}
if ($bannerFrames -gt 49092) {
    throw "banner.wav is $([math]::Round($bannerFrames / 16364.0, 2)) seconds; the HOME menu allows three."
}

& $Bannertool makebanner -i $bannerPng -a $bannerWav -o $banner
if ($LASTEXITCODE -ne 0) {
    throw "bannertool failed with exit code $LASTEXITCODE"
}

if (Test-Path -LiteralPath $output) {
    Remove-Item -LiteralPath $output -Force
}

& $Makerom -f cia -o $output -target t -desc app:2.50 `
    -rsf $rsf -elf $elf -icon $smdh -banner $banner `
    "-DDIR_ROMFS=$romfs" "-DAPP_UNIQUE_ID=$UniqueId"
if ($LASTEXITCODE -ne 0) {
    throw "makerom failed with exit code $LASTEXITCODE"
}

Get-Item -LiteralPath $output | Select-Object FullName, Length, LastWriteTime
