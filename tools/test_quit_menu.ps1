# Teste da saida pelo botao de voltar no menu principal (FUNCOES.md #86): boot ate o escritorio (drive.ps1),
# print do rodape, V (voltar) abre "Quit game?", Esc cancela, V de novo, A (Yes), Enter -> o jogo deve fechar.
# Saida: uma linha OK/FALHA + pasta dos prints. Nao use o PC durante o teste (manda teclas para a janela).
param([int]$Seconds = 300, [switch]$Software)
$root = Split-Path -Parent $PSScriptRoot
$env:PS2X_GS_BACKEND = if ($Software) { 'software' } else { 'parallel' }
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$shots = Join-Path $root "logs\quitmenu_$stamp"
# t:vk:hold a partir do menu; cada tecla gera um print. 0x7C (F13) nao faz nada: so o print do rodape.
$keys = '6:7C:50,8:56:200,11:1B:200,14:56:200,17:41:200,20:0D:200'
$out = & (Join-Path $PSScriptRoot 'drive.ps1') -Seconds $Seconds -OutDir $shots -MenuKeys $keys *>&1 | ForEach-Object { "$_" }
$log = ($out | Select-String '^log: (.+)$' | Select-Object -Last 1).Matches.Groups[1].Value
$menu = $out | Select-String 'menu detectado' | Select-Object -First 1
$exited = $out | Select-String 'processo saiu' -Quiet
$confirm = $log -and (Select-String -Path $log -Pattern 'confirmacao de saida' -Quiet)
$exitMsg = $log -and (Select-String -Path $log -Pattern 'Exit to Windows' -Quiet)
$modes = if ($log) { (Select-String -Path $log -Pattern '\[rdr\] prompts (\S+)' | ForEach-Object { $_.Matches.Groups[1].Value }) -join ' ' } else { '' }
$res = if ($exited -and $confirm -and $exitMsg) { 'OK' } else { 'FALHA' }
"${res}: menu=$(if ($menu) { $menu.Line.Trim() } else { 'nao' }) caixa=$confirm saiu=$exited | modos: $modes"
"prints: $shots | log: $log"
