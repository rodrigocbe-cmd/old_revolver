# Roda um script de tools/ghidra no projeto headless (somente leitura). Uso:
#   .\tools\ghidra_run.ps1 DecompileFuncs.java out\ghidra\decomp_x.txt 470540 4928d8
param([Parameter(Mandatory)][string]$Script, [Parameter(ValueFromRemainingArguments)][string[]]$ScriptArgs)
$root = Split-Path -Parent $PSScriptRoot
$jdk = Get-ChildItem 'C:\Program Files\Eclipse Adoptium' -Directory -ErrorAction SilentlyContinue | Where-Object Name -like 'jdk-21*' | Select-Object -First 1
if ($jdk) { $env:JAVA_HOME = $jdk.FullName }
$ghidra = Get-ChildItem "$env:LOCALAPPDATA\Programs" -Directory -Filter 'ghidra_*_PUBLIC' | Sort-Object Name | Select-Object -Last 1 -ExpandProperty FullName
$log = Join-Path $root 'logs\ghidra_run.log'
& "$ghidra\support\analyzeHeadless.bat" (Join-Path $root 'out\ghidra') RDR -process SLUS_205.00.unpacked.elf -noanalysis -readOnly `
    -scriptPath (Join-Path $root 'tools\ghidra') -postScript $Script @ScriptArgs 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $log
Select-String -Path $log -Pattern 'ERROR|Exception' | Select-Object -First 3 | ForEach-Object { $_.Line.Trim() }
