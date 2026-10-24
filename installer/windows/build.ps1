# Builds the Windows setup and the portable zip from a built tree: .\installer\windows\build.ps1 -Version 0.0.3 -Source build\ui\Release
# Needs Inno Setup 6 (winget install JRSoftware.InnoSetup, or choco install innosetup).
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Source,   # folder with jad-daw.exe; the tests and the import libraries are not copied
    [string]$Out = "dist"
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$parent = Join-Path $env:TEMP "jad-daw-stage-$Version"
$stage = Join-Path $parent "jad-daw-$Version-windows-x64"
if (Test-Path $parent) { Remove-Item -Recurse -Force $parent }
New-Item -ItemType Directory -Path $stage | Out-Null
$src = Resolve-Path $Source
# the program and what it loads: everything of the build folder except the test programs and the link files
Get-ChildItem $src | Where-Object { $_.Name -notmatch '^jad_.*_tests\.exe$|^lpc_tests\.exe$|\.(lib|exp|pdb|ilk)$' } | Copy-Item -Destination $stage -Recurse
Copy-Item (Join-Path $root "LICENSE"), (Join-Path $root "THIRD_PARTY.md") $stage
Copy-Item (Join-Path $root "assets\fonts\LICENSE.txt") (Join-Path $stage "Inter-LICENSE.txt")
$iscc = @("$env:ProgramFiles(x86)\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) not found" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$outAbs = Resolve-Path $Out
Compress-Archive -Path $stage -DestinationPath (Join-Path $outAbs "jad-daw-$Version-windows-x64.zip") -Force
& $iscc "/DAppVersion=$Version" "/DSourceDir=$stage" "/DOutputDir=$outAbs" (Join-Path $PSScriptRoot "jad-daw.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
Remove-Item -Recurse -Force $parent
Get-ChildItem $outAbs -Filter "jad-daw-$Version-windows-x64*"
