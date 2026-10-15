# Carrega o ambiente do MSVC x64 (vcvars64) e o JDK na sessao atual do PowerShell.
# Uso: . .\scripts\env.ps1   (dot-source)
# Obs.: manter este arquivo em ASCII (o PowerShell 5.1 le .ps1 sem BOM como ANSI).

$ErrorActionPreference = 'Stop'

# O instalador do VS tambem registra outros produtos (ex.: SSMS); pega a instancia que tem o vcvars64.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -all -products * -property installationPath |
    Where-Object { Test-Path (Join-Path $_ 'VC\Auxiliary\Build\vcvars64.bat') } | Select-Object -First 1
if (-not $vsPath) { throw "vcvars64.bat nao encontrado (instale a carga 'Desenvolvimento para desktop com C++')" }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'

if (-not $env:VCToolsVersion) {
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

# CMake que acompanha o Visual Studio, caso nao haja um no PATH
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $env:PATH = (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin') + ";$env:PATH"
}
# Ninja instalado pelo winget pode nao estar no PATH de shells antigos
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $ninja = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ninja) { $env:PATH = "$($ninja.DirectoryName);$env:PATH" }
}

$jdk = Get-ChildItem 'C:\Program Files\Eclipse Adoptium' -Directory -ErrorAction SilentlyContinue | Where-Object Name -like 'jdk-21*' | Select-Object -First 1
if ($jdk) { $env:JAVA_HOME = $jdk.FullName }

$Global:RDR_ROOT = Split-Path -Parent $PSScriptRoot
# logs/ e ignorado pelo git; num clone novo ainda nao existe
New-Item -ItemType Directory -Force (Join-Path $Global:RDR_ROOT 'logs') | Out-Null
$Global:GHIDRA_HOME = Get-ChildItem "$env:LOCALAPPDATA\Programs" -Directory -Filter 'ghidra_*_PUBLIC' | Sort-Object Name | Select-Object -Last 1 -ExpandProperty FullName
