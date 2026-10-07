# Installs Qt into .\.qt with aqtinstall (no account, no GUI). Usage: tools\setup-qt.ps1 [-Version 6.8.3]
param([string]$Version = "6.8.3")
$ErrorActionPreference = "Stop"
py -m venv .qt-venv
.\.qt-venv\Scripts\pip install --quiet aqtinstall
.\.qt-venv\Scripts\aqt install-qt windows desktop $Version win64_msvc2022_64 --outputdir .qt -m qtshadertools
$arch = (Get-ChildItem ".qt\$Version" | Select-Object -First 1).Name
Write-Host "Qt installed. Configure with: -DCMAKE_PREFIX_PATH=$((Get-Location).Path)\.qt\$Version\$arch"
