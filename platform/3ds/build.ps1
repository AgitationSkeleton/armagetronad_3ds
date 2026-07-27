param(
    [switch]$Clean,
    [switch]$Cia
)

$ErrorActionPreference = 'Stop'

$devkitPro = if ($env:DEVKITPRO -and (Test-Path -LiteralPath $env:DEVKITPRO)) {
    (Resolve-Path -LiteralPath $env:DEVKITPRO).Path
} else {
    'C:\devkitPro'
}
$devkitArm = if ($env:DEVKITARM -and (Test-Path -LiteralPath $env:DEVKITARM)) {
    (Resolve-Path -LiteralPath $env:DEVKITARM).Path
} else {
    Join-Path $devkitPro 'devkitARM'
}
$make = Join-Path $devkitPro 'msys2\usr\bin\make.exe'
$cygpath = Join-Path $devkitPro 'msys2\usr\bin\cygpath.exe'
$project = $PSScriptRoot
$prepareRomfs = Join-Path $project "prepare-romfs.ps1"

if (-not (Test-Path -LiteralPath $make)) {
    throw "GNU make was not found at $make"
}

if (-not (Test-Path -LiteralPath $cygpath)) {
    throw "MSYS2 cygpath was not found at $cygpath"
}

if (-not (Test-Path -LiteralPath (Join-Path $devkitArm '3ds_rules'))) {
    throw "devkitARM 3DS rules were not found at $devkitArm"
}

function Convert-ToMsysPath([string]$Path) {
    $converted = & $cygpath -u $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Could not convert $Path to an MSYS2 path"
    }
    return $converted.Trim()
}

$env:DEVKITPRO = Convert-ToMsysPath $devkitPro
$env:DEVKITARM = Convert-ToMsysPath $devkitArm
$env:CTRULIB = Convert-ToMsysPath (Join-Path $devkitPro 'libctru')

Push-Location $project
try {
    & $prepareRomfs

    $threeDsx = Join-Path $project 'armagetronad-3ds.3dsx'
    if (Test-Path -LiteralPath $threeDsx) {
        Remove-Item -LiteralPath $threeDsx -Force
    }

    if ($Clean) {
        & $make clean
        if ($LASTEXITCODE -ne 0) {
            throw "Clean failed with exit code $LASTEXITCODE"
        }
    }

    & $make
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }

    if ($Cia) {
        & (Join-Path $project 'package-cia.ps1')
        if ($LASTEXITCODE -ne 0) {
            throw "CIA packaging failed with exit code $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}
