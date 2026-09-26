param(
    [ValidateSet('Production', 'TestMode', 'All')]
    [string]$Configuration = 'All'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$sketchPath = Join-Path $projectRoot 'firmware\Millivolt_Monitor\Millivolt_Monitor.ino'
$firmwareRoot = Split-Path -Parent (Split-Path -Parent $sketchPath)
$outputRoot = Join-Path $firmwareRoot 'builds'
$workingRoot = Join-Path $firmwareRoot '.build'

$arduinoRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\arduino'
$avrRoot = Join-Path $arduinoRoot 'hardware\avr\1.8.8'
$toolRoot = Join-Path $arduinoRoot 'tools\avr-gcc\7.3.0-atmel3.6.1-arduino7\bin'
$coreRoot = Join-Path $avrRoot 'cores\arduino'
$variantRoot = Join-Path $avrRoot 'variants\standard'
$spiRoot = Join-Path $avrRoot 'libraries\SPI\src'
$sdLibraryRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\libraries\SD'
$sdRoot = Join-Path $sdLibraryRoot 'src'
$sdPropertiesPath = Join-Path $sdLibraryRoot 'library.properties'
$expectedSdVersion = '1.3.0'

$avrGcc = Join-Path $toolRoot 'avr-gcc.exe'
$avrGpp = Join-Path $toolRoot 'avr-g++.exe'
$avrAr = Join-Path $toolRoot 'avr-gcc-ar.exe'
$avrObjcopy = Join-Path $toolRoot 'avr-objcopy.exe'
$avrSize = Join-Path $toolRoot 'avr-size.exe'

$required = @(
    $sketchPath, $avrGcc, $avrGpp, $avrAr, $avrObjcopy, $avrSize,
    (Join-Path $coreRoot 'Arduino.h'), (Join-Path $spiRoot 'SPI.cpp'),
    (Join-Path $sdRoot 'SD.cpp'), $sdPropertiesPath
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required Arduino build dependency is missing: $path"
    }
}

$sdVersionLine = Get-Content -LiteralPath $sdPropertiesPath | Where-Object { $_ -match '^version=' } | Select-Object -First 1
if ($null -eq $sdVersionLine) {
    throw "Unable to determine Arduino SD library version from: $sdPropertiesPath"
}
$sdVersion = ($sdVersionLine -split '=', 2)[1].Trim()
if ($sdVersion -ne $expectedSdVersion) {
    throw "Arduino SD library version mismatch. Expected $expectedSdVersion, found $sdVersion at $sdLibraryRoot"
}

Write-Host "Dependencies: Arduino AVR Boards 1.8.8; SD $sdVersion; avr-gcc 7.3.0-atmel3.6.1-arduino7"

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $workingRoot | Out-Null

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$Program,
        [Parameter(Mandatory)] [string[]]$Arguments
    )
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build command failed ($LASTEXITCODE): $Program"
    }
}

function Get-ObjectName {
    param([string]$Prefix, [System.IO.FileInfo]$Source)
    $safeDirectory = $Source.Directory.Name -replace '[^A-Za-z0-9_]', '_'
    return "${Prefix}_${safeDirectory}_$($Source.BaseName).o"
}

function Build-Configuration {
    param(
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] [bool]$TestMode
    )

    $buildRoot = Join-Path $workingRoot $Name
    if (Test-Path -LiteralPath $buildRoot) {
        $resolvedBuild = [System.IO.Path]::GetFullPath($buildRoot)
        $resolvedWorking = [System.IO.Path]::GetFullPath($workingRoot)
        if (-not $resolvedBuild.StartsWith($resolvedWorking, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean unexpected build path: $resolvedBuild"
        }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

    $defines = @(
        '-DF_CPU=16000000L', '-DARDUINO=10819', '-DARDUINO_AVR_UNO',
        '-DARDUINO_ARCH_AVR'
    )
    if ($TestMode) { $defines += '-DTEST_MODE=1' }

    $includes = @(
        "-I$coreRoot", "-I$variantRoot", "-I$spiRoot", "-I$sdRoot"
    )
    $common = @('-g', '-Os', '-ffunction-sections', '-fdata-sections', '-flto', '-mmcu=atmega328p') + $defines + $includes
    $cppFlags = @('-c', '-std=gnu++11', '-fpermissive', '-fno-exceptions', '-fno-threadsafe-statics', '-Wno-error=narrowing', '-MMD') + $common
    $cFlags = @('-c', '-std=gnu11', '-fno-fat-lto-objects', '-MMD') + $common
    $asmFlags = @('-c', '-x', 'assembler-with-cpp', '-MMD', '-flto', '-mmcu=atmega328p') + $defines + $includes

    $sketchObject = Join-Path $buildRoot 'sketch.o'
    Invoke-Checked $avrGpp ($cppFlags + @('-include', 'Arduino.h', '-x', 'c++', $sketchPath, '-o', $sketchObject))

    $coreObjects = [System.Collections.Generic.List[string]]::new()
    foreach ($source in Get-ChildItem -LiteralPath $coreRoot -File | Where-Object { $_.Extension -in '.c', '.cpp', '.S' }) {
        $object = Join-Path $buildRoot (Get-ObjectName 'core' $source)
        if ($source.Extension -eq '.c') {
            Invoke-Checked $avrGcc ($cFlags + @($source.FullName, '-o', $object))
        } elseif ($source.Extension -eq '.S') {
            Invoke-Checked $avrGcc ($asmFlags + @($source.FullName, '-o', $object))
        } else {
            Invoke-Checked $avrGpp ($cppFlags + @($source.FullName, '-o', $object))
        }
        $coreObjects.Add($object)
    }

    $coreArchive = Join-Path $buildRoot 'core.a'
    foreach ($object in $coreObjects) {
        Invoke-Checked $avrAr @('rcs', $coreArchive, $object)
    }

    $libraryObjects = [System.Collections.Generic.List[string]]::new()
    $librarySources = @(
        Get-Item -LiteralPath (Join-Path $spiRoot 'SPI.cpp')
        Get-ChildItem -LiteralPath $sdRoot -Filter '*.cpp' -File
        Get-ChildItem -LiteralPath (Join-Path $sdRoot 'utility') -Filter '*.cpp' -File
    )
    foreach ($source in $librarySources) {
        $object = Join-Path $buildRoot (Get-ObjectName 'lib' $source)
        Invoke-Checked $avrGpp ($cppFlags + @($source.FullName, '-o', $object))
        $libraryObjects.Add($object)
    }

    $elfPath = Join-Path $outputRoot "Millivolt_Monitor_${Name}.elf"
    $hexPath = Join-Path $outputRoot "Millivolt_Monitor_${Name}.hex"
    $linkArgs = @('-Os', '-g', '-flto', '-fuse-linker-plugin', '-Wl,--gc-sections', '-mmcu=atmega328p', '-o', $elfPath, $sketchObject)
    $linkArgs += $libraryObjects
    $linkArgs += @($coreArchive, '-lm')
    Invoke-Checked $avrGcc $linkArgs
    Invoke-Checked $avrObjcopy @('-O', 'ihex', '-R', '.eeprom', $elfPath, $hexPath)

    Write-Host "`n$Name"
    Invoke-Checked $avrSize @('-C', '--mcu=atmega328p', $elfPath)
}

if ($Configuration -in 'Production', 'All') { Build-Configuration -Name 'Production' -TestMode $false }
if ($Configuration -in 'TestMode', 'All') { Build-Configuration -Name 'TestMode' -TestMode $true }

$resolvedWorkingRoot = [System.IO.Path]::GetFullPath($workingRoot)
$resolvedFirmwareRoot = [System.IO.Path]::GetFullPath($firmwareRoot)
if (-not $resolvedWorkingRoot.StartsWith($resolvedFirmwareRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to remove unexpected working path: $resolvedWorkingRoot"
}
Remove-Item -LiteralPath $resolvedWorkingRoot -Recurse -Force

Write-Host "`nBuilds written to: $outputRoot"
