# Imports a raw STM32L15xxD image into the Ghidra project, sets up memory/SVD/vectors,
# runs auto-analysis and exports decompiled text.
#
#   tools\ghidra_import.ps1 -Image dumps\bootloader.bin -Base 0x08000000 -Name bootloader
param(
    [Parameter(Mandatory)] [string] $Image,
    [Parameter(Mandatory)] [string] $Base,
    [Parameter(Mandatory)] [string] $Name
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:JAVA_HOME = 'C:\Users\Dave\Tools\jdk-21.0.13+9'
$headless = 'C:\Users\Dave\Tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat'
$svd = 'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\SVD\STM32L152.svd'
$out = Join-Path $root "ghidra\export\$Name"

& $headless (Join-Path $root 'ghidra') 121GW `
    -import (Resolve-Path $Image) -overwrite `
    -processor 'ARM:LE:32:Cortex' -loader BinaryLoader -loader-baseAddr $Base `
    -scriptPath (Join-Path $root 'tools\ghidra_scripts') `
    -preScript SetupSTM32.java $svd `
    -postScript ExportDecomp.java $out
