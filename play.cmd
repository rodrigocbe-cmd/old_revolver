@echo off
rem Abre o jogo (tools\play.ps1) sem depender da politica de execucao de scripts do PowerShell.
rem Argumentos passam adiante, ex.: play.cmd -Ssaa 16 -Size 2560x1440
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\play.ps1" %*
