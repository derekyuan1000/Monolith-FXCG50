<#
.SYNOPSIS
  One-command Monolith build from Windows (runs the tools/*.sh scripts inside WSL).

.DESCRIPTION
  Runs only the steps that are needed:
    1. tools/build_stock.sh   when the toolchain or ~/khicas-work/giacbf.tgz is missing (first time, ~5 min)
    2. tools/fork_setup.sh    when ~/khicas-work/fork does not exist yet
    3. tools/build_fork.sh    always (incremental)
    4. tools/package_release.sh  unless -NoPackage
  Outputs: build/fork/monolith.g3a, build/fork/monolith.ac2 and build/release/monolith-<version>.zip

.PARAMETER Distro
  WSL distribution to use (default: Ubuntu).

.PARAMETER Refork
  Delete ~/khicas-work/fork and re-apply patches/ (use after pulling new patches).
  Refuses to run if the fork tree has uncommitted changes.

.PARAMETER NoPackage
  Skip the release zip.

.PARAMETER Prof
  Profiling build: the status bar shows F<screen flips> T<RTC ticks, 1/128 s> for the last key.

.PARAMETER Core
  Core-loop build: overlays (fraction editor, S<->D, F-key sub-bars, pinned) and 2D history are compiled out.

.EXAMPLE
  .\build.ps1
.EXAMPLE
  .\build.ps1 -Refork
#>
param(
  [string]$Distro = 'Ubuntu',
  [switch]$Refork,
  [switch]$NoPackage,
  [switch]$Prof,
  [switch]$Core
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Invoke-Wsl([string]$Command) {
  wsl -d $Distro -- bash -lc $Command
  if ($LASTEXITCODE -ne 0) { throw "WSL step failed (exit $LASTEXITCODE): $Command" }
}
function Test-Wsl([string]$Command) {
  wsl -d $Distro -- bash -lc $Command 2>$null
  return $LASTEXITCODE -eq 0
}

if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
  throw 'WSL is not installed. Run "wsl --install -d Ubuntu" in an admin PowerShell, reboot, then retry.'
}
if (-not (Test-Wsl 'true')) {
  throw "WSL distribution '$Distro' is not available. List them with 'wsl -l -v' or pass -Distro <name>."
}

# Run from the repo root inside WSL: build_stock.sh uses $(pwd) as the repo.
$repo = (wsl -d $Distro -- wslpath -a ($PSScriptRoot -replace '\\', '/')).Trim()
$cd = "cd '$repo' &&"

$haveToolchain = Test-Wsl '[ -x "$HOME/opt/sh3eb-elf/bin/sh3eb-elf-gcc" ] && [ -f "${KHICAS_WORK:-$HOME/khicas-work}/giacbf.tgz" ] && command -v mkg3a >/dev/null'
if (-not $haveToolchain) {
  Write-Host '== First-time setup: toolchain + pristine sources (asks for your WSL sudo password)' -ForegroundColor Cyan
  Invoke-Wsl "$cd bash tools/build_stock.sh"
}

$haveFork = Test-Wsl '[ -d "${KHICAS_WORK:-$HOME/khicas-work}/fork/.git" ]'
if ($haveFork -and $Refork) {
  $dirty = wsl -d $Distro -- bash -lc 'cd "${KHICAS_WORK:-$HOME/khicas-work}/fork" && git status --porcelain'
  if ($dirty) { throw "The fork tree has uncommitted changes; commit them (and run tools/export_patches.sh) before -Refork." }
  Write-Host '== Removing the fork tree to re-apply patches/' -ForegroundColor Cyan
  Invoke-Wsl 'rm -rf "${KHICAS_WORK:-$HOME/khicas-work}/fork"'
  $haveFork = $false
}
if (-not $haveFork) {
  Write-Host '== Applying patches/ to a fresh fork tree' -ForegroundColor Cyan
  Invoke-Wsl "$cd bash tools/fork_setup.sh"
}

Write-Host '== Building' -ForegroundColor Cyan
$switches = "MONO_PROF=$([int]$Prof.IsPresent) MONO_CORE=$([int]$Core.IsPresent)"
Invoke-Wsl "$cd $switches bash tools/build_fork.sh"

if (-not $NoPackage) {
  Write-Host '== Packaging' -ForegroundColor Cyan
  Invoke-Wsl "$cd bash tools/package_release.sh"
}
Write-Host "Done. Add-in files are in $PSScriptRoot\build\fork\" -ForegroundColor Green
