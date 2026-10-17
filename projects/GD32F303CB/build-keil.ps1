$ErrorActionPreference = 'Stop'

$projectDir = $PSScriptRoot
$sdkDir = Join-Path (Split-Path (Split-Path $projectDir -Parent) -Parent) 'sdk\GD32F30x_Firmware_Library_V3.0.3'
$toolDir = 'C:\Keil_v5\ARM\ARMCC\bin'
$buildDir = Join-Path $projectDir 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

$includes = @(
    "-I$projectDir",
    "-I$(Join-Path $sdkDir 'Firmware\CMSIS')",
    "-I$(Join-Path $sdkDir 'Firmware\CMSIS\GD\GD32F30x\Include')",
    "-I$(Join-Path $sdkDir 'Firmware\GD32F30x_standard_peripheral\Include')"
)
$common = @('--cpu', 'Cortex-M4.fp.sp', '--apcs=interwork', '-c', '--c99', '--gnu', '-DGD32F30X_HD', '-DUSE_STDPERIPH_DRIVER') + $includes
$sources = @(
    @{ Name = 'main'; Path = Join-Path $projectDir 'main.c' },
    @{ Name = 'system_gd32f30x'; Path = Join-Path $projectDir 'system_gd32f30x.c' },
    @{ Name = 'gd32f30x_gpio'; Path = Join-Path $sdkDir 'Firmware\GD32F30x_standard_peripheral\Source\gd32f30x_gpio.c' },
    @{ Name = 'gd32f30x_rcu'; Path = Join-Path $sdkDir 'Firmware\GD32F30x_standard_peripheral\Source\gd32f30x_rcu.c' }
)
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $buildDir ($source.Name + '.o')
    & (Join-Path $toolDir 'armcc.exe') @common -o $object $source.Path
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $($source.Path)" }
    $objects += $object
}

$startup = Join-Path $buildDir 'startup_gd32f30x_hd.o'
& (Join-Path $toolDir 'armasm.exe') --cpu Cortex-M4.fp.sp -o $startup (Join-Path $sdkDir 'Firmware\CMSIS\GD\GD32F30x\Source\ARM\startup_gd32f30x_hd.s')
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$objects += $startup

$axf = Join-Path $buildDir 'GD32F303CB.axf'
$hex = Join-Path $buildDir 'GD32F303CB.hex'
& (Join-Path $toolDir 'armlink.exe') --cpu Cortex-M4.fp.sp --scatter (Join-Path $projectDir 'GD32F303CB.sct') --info sizes -o $axf @objects
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
& (Join-Path $toolDir 'fromelf.exe') --i32combined --output $hex $axf
if ($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
Write-Host "Build OK: $axf"
Write-Host "HEX: $hex"
