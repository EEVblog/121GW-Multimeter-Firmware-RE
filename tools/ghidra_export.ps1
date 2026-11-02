# Re-exports decompiled text for a program already in the Ghidra project
# (after renaming or retyping), without re-importing.
#
#   tools\ghidra_export.ps1 -Program bootloader.bin -Name bootloader
param(
    [Parameter(Mandatory)] [string] $Program,
    [Parameter(Mandatory)] [string] $Name,
    [string[]] $PreScripts = @()
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:JAVA_HOME = 'C:\Users\Dave\Tools\jdk-21.0.13+9'
$headless = 'C:\Users\Dave\Tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat'
$out = Join-Path $root "ghidra\export\$Name"

$scriptArgs = @()
foreach ($s in $PreScripts) { $scriptArgs += @('-postScript') + ($s -split ' ') }

& $headless (Join-Path $root 'ghidra') 121GW -process $Program -noanalysis `
    -scriptPath (Join-Path $root 'tools\ghidra_scripts') `
    @scriptArgs -postScript ExportDecomp.java $out
