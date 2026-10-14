# Roda o executavel recompilado por N segundos e salva o log em logs/.
# game/disc e a raiz do cdrom0: (conteudo do disco extraido por tools/iso_extract.py).
# O runtime carrega o ELF que recebe e usa a pasta dele como cdrom0:, entao em game/disc
# o SLUS_205.00 original (comprimido) vira SLUS_205.00.packed e o descomprimido assume o nome.
param([int]$Seconds = 30)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$disc = Join-Path $root 'game\disc'
$elf = Join-Path $disc 'SLUS_205.00'
$packed = Join-Path $disc 'SLUS_205.00.packed'
$unpacked = Join-Path $root 'game\SLUS_205.00.unpacked.elf'

if (-not (Test-Path $disc)) { throw 'game/disc nao existe: python tools/iso_extract.py extract <iso> game/disc' }
if (-not (Test-Path $packed)) { Move-Item $elf $packed }
if (-not (Test-Path $elf) -or (Get-FileHash $elf).Hash -ne (Get-FileHash $unpacked).Hash) {
    Copy-Item $unpacked $elf -Force
}

$exe = Get-ChildItem (Join-Path $root 'out\build') -Recurse -Filter ps2EntryRunner.exe | Select-Object -First 1
if (-not $exe) { throw 'ps2EntryRunner.exe nao encontrado: rode 3_build.ps1' }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $root "logs\run_$stamp.log"
$p = Start-Process -FilePath $exe.FullName -ArgumentList "`"$elf`"" -WorkingDirectory $disc `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
if (-not $p.WaitForExit($Seconds * 1000)) { Stop-Process -Id $p.Id -Force; "encerrado apos $Seconds s" }
else { "saiu com codigo $($p.ExitCode)" }
Get-Content "$log.err" | Add-Content $log
Remove-Item "$log.err"
Get-Content $log -Tail 40
"log: $log"
