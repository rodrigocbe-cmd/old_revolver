# Monta a versao de distribuicao em dist\RedDeadRevolver-v<versao>\ (+ .zip). Nada do jogo vai junto: na 1a
# execucao o lancador pede a ISO do jogador e instala os arquivos nas pastas (docs/LAYOUT.md).
# Uso: .\tools\make_dist.ps1 [-Version 1.0.0]
param([string]$Version = "1.0.0")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'out\build\ps2xRuntime'
$exe = Join-Path $build 'ps2EntryRunner.exe'
if (-not (Test-Path $exe)) { throw "build nao encontrado: $exe (rode scripts\3_build.ps1)" }
$name = "RedDeadRevolver-v$Version"
# Monta numa pasta temporaria: dist\$name pode ter uma instalacao do jogador (system\, movies\, audio\...), que nao pode
# ser apagada nem entrar no zip. Ao final, so os arquivos do programa sao copiados por cima.
$installed = Join-Path $root "dist\$name"
$stage = Join-Path $root "dist\.stage"
$dist = Join-Path $stage $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $dist, (Join-Path $dist 'launcher') | Out-Null

Copy-Item $exe (Join-Path $dist 'RedDeadRevolver.exe')
# Mesmo binario: pelo nome, abre o lancador (escolha da ROM) em vez do jogo, como o GTA Vice City
Copy-Item $exe (Join-Path $dist 'RDR Launcher.exe')
Get-ChildItem $build -Filter *.dll | Copy-Item -Destination $dist
Copy-Item (Join-Path $root 'assets\launcher\Rye-Regular.ttf'), (Join-Path $root 'assets\launcher\OFL.txt') (Join-Path $dist 'launcher')
# Conquistas (tela do lancador): lista e imagens
New-Item -ItemType Directory -Force (Join-Path $dist 'achievements') | Out-Null
Copy-Item (Join-Path $root 'achievements\achievements.json') (Join-Path $dist 'achievements')
Copy-Item (Join-Path $root 'achievements\imagens') (Join-Path $dist 'achievements') -Recurse
# Artes da colecao SteamGridDB (assets\SGDB, fora do git) para o "Add to Steam" do lancador
$sgdb = Join-Path $root 'assets\SGDB'
$art = @{ 'capsule.png' = 'RD Revolver - Capsules.png'; 'wide.png' = 'RD Revolver - Capsules 2.png'; 'hero.png' = 'RD Revolver - Hero.png'; 'logo.png' = 'RD Revolver - Logo.png'; 'icon.png' = 'RD Revolver - Icon.png' }
New-Item -ItemType Directory -Force (Join-Path $dist 'steam') | Out-Null
foreach ($k in $art.Keys) {
    $src = Join-Path $sgdb $art[$k]
    if (Test-Path $src) { Copy-Item $src (Join-Path $dist "steam\$k") } else { Write-Warning "arte Steam ausente: $src" }
}

@"
RED DEAD REVOLVER - PC $("v$Version")
Static recompilation of the PlayStation 2 game (PS2Recomp). Unofficial fan project.

YOU NEED YOUR OWN COPY OF THE GAME: a disc image (.iso) of Red Dead Revolver (USA, SLUS-20500)
made from your own disc. No game files are included.

FIRST RUN
  1. Run RDR Launcher.exe (or RedDeadRevolver.exe, which opens the launcher while the game is not installed).
  2. Choose SELECT ROM and pick your .iso. The game files are installed into this folder (~2.3 GB).
  3. Choose PLAY. After that the .iso is no longer needed.

RedDeadRevolver.exe starts the game directly. RDR Launcher.exe lets you install from a different ROM.
ADD TO STEAM (after installing) adds the game to Steam as a non-Steam game, with artwork.

FOLDERS (created by the installer, like GTA Vice City for PC)
  movies\          videos (*.PSS)
  audio\           music, speech and sound banks (STREAMS.DAT, BANKS.DAT)
  data\            game data (SWAT.DAT)
  frontend\        menus and fonts (FRONT.DAT)
  levels\single\   story levels       levels\mp2\, levels\mp4\  multiplayer arenas
  extras\          bonus content (PUPSHOW.DAT)
  system\          boot files and the disc index (do not edit)
  launcher\        launcher font (Rye, SIL Open Font License, see OFL.txt)
  steam\           artwork used by ADD TO STEAM (SteamGridDB collection 34157)

SAVES
  Documents\Red Dead Revolver\  (saved and loaded automatically)

CONTROLS
  Gamepad (XInput) or keyboard/mouse. F2 releases the mouse.

---------------------------------------------------------------------------------------------------
PORTUGUES: precisa da sua propria ISO (USA, SLUS-20500). Rode RedDeadRevolver.exe, escolha
SELECT ROM, depois PLAY. Depois disso o RedDeadRevolver.exe abre o jogo direto e o RDR Launcher.exe
abre o lancador. Os saves ficam em Documentos\Red Dead Revolver.
"@ | Set-Content -Encoding utf8 (Join-Path $dist 'README.txt')

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $dist -DestinationPath $zip
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
New-Item -ItemType Directory -Force $installed | Out-Null
Copy-Item (Join-Path $dist '*') $installed -Recurse -Force
Remove-Item $stage -Recurse -Force
"pacote: $installed"
"zip:    $zip ($size MB)"
