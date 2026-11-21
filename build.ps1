# Builds Monolith from Windows through WSL. See tools/build.sh.
#   .\build.ps1            build (first run also installs the toolchain)
#   .\build.ps1 -Clean     start again from a fresh KhiCAS tree
#   .\build.ps1 -Distro X  if your WSL distribution is not called Ubuntu
param([switch]$Clean, [string]$Distro = "Ubuntu")
$ErrorActionPreference = "Stop"
$repo = (wsl -d $Distro -- wslpath -a ($PSScriptRoot -replace '\\', '/')).Trim()
$flags = @()
if ($Clean) { $flags += "--clean" }
wsl -d $Distro -- bash "$repo/tools/build.sh" @flags
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Done: build\monolith.g3a, build\monolith.ac2 and build\release\"
