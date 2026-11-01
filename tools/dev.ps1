<#
  ProsperoTV - Windows front end of tools/dev.sh: the development container from PowerShell.
  Copyright (C) 2026 ThatGuyKalvin
  SPDX-License-Identifier: GPL-3.0-or-later

  usage: .\tools\dev.ps1 <command> [arguments]      (docs/DEVELOPMENT.md)

  Commands, variables and outputs are those of tools/dev.sh: image, doctor,
  test, build, deploy [--full], all, preview [w h], lint, legacy-test, shell.
  Needs Docker Desktop or Podman Desktop; WSL is not used directly.
#>

#requires -Version 5.1
$ErrorActionPreference = "Stop"

function Fail([string]$Message) {
    [Console]::Error.WriteLine("dev.ps1: $Message")
    exit 2
}

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$command = if ($args.Count -gt 0) { $args[0] } else { "help" }
$rest = @(if ($args.Count -gt 1) { $args[1..($args.Count - 1)] })

$engine = $env:CONTAINER
if (-not $engine) {
    if (Get-Command podman -ErrorAction SilentlyContinue) { $engine = "podman" }
    elseif (Get-Command docker -ErrorAction SilentlyContinue) { $engine = "docker" }
    else { Fail "install Docker Desktop or Podman first (docs/DEVELOPMENT.md)" }
}
$image = if ($env:IMAGE) { $env:IMAGE } else { "prosperotv-dev" }
$volume = if ($env:VOLUME) { $env:VOLUME } else { "prosperotv-work" }

# The environment wins over .env.
function Get-Setting([string]$Name) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ($value) { return $value }
    $file = Join-Path $root ".env"
    if (Test-Path $file) {
        $line = Get-Content $file | Where-Object { $_ -match "^\s*$Name\s*=" } | Select-Object -Last 1
        if ($line) { return ($line -replace "^\s*$Name\s*=\s*", "").Trim().Trim('"', "'") }
    }
    return ""
}

function Invoke-Inside([string[]]$Steps) {
    $build = Join-Path $root "build"
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    $tty = @()
    if (-not [Console]::IsInputRedirected -and -not [Console]::IsOutputRedirected) { $tty = @("-it") }
    $tree = if ($env:TREE) { $env:TREE } else { "/w/app-build" }
    & $engine run --rm @tty `
        -v "${root}:/src:ro" -v "${build}:/out" -v "${volume}:/w" `
        -e "PS5_HOST=$(Get-Setting PS5_HOST)" -e "FTP_PORT=$(Get-Setting FTP_PORT)" `
        -e "BUILD_JOBS=$(Get-Setting BUILD_JOBS)" -e "TREE=$tree" `
        -e "HOST_SANITIZE=$env:HOST_SANITIZE" -e "LINT_SKIP_TIDY=$env:LINT_SKIP_TIDY" `
        -e "TV_LANGUAGE_OVERRIDE=$env:TV_LANGUAGE_OVERRIDE" -e "TV_SYSTEM_LANGUAGE=$env:TV_SYSTEM_LANGUAGE" `
        $image bash /src/tools/dev/inside.sh @Steps
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Assert-Image {
    & $engine image inspect $image *> $null
    if ($LASTEXITCODE -ne 0) { Fail "no $image image yet: run .\tools\dev.ps1 image" }
}

# The upload runs on the host when it has Python: a container engine's VM may not
# reach the local network, and the host always can.
function Invoke-Deploy([string[]]$Arguments) {
    $python = @("python3", "python", "py") | Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if (-not $python) { Assert-Image; Invoke-Inside (@("deploy") + $Arguments); return }
    $id = (Get-Content (Join-Path $root "sce_sys/param.json") -Raw | ConvertFrom-Json).titleId
    $folder = Join-Path $root "build/dev/$id"
    if (-not (Test-Path (Join-Path $folder "eboot.bin"))) { Fail "no build yet: run .\tools\dev.ps1 build" }
    $env:PS5_HOST = Get-Setting PS5_HOST
    $env:FTP_PORT = Get-Setting FTP_PORT
    & $python (Join-Path $root "tools/dev/ps5-deploy.py") $folder --manifests (Join-Path $root ".local/deploy") @Arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-Timed([string]$Name, [string[]]$Steps) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Invoke-Inside $Steps
    Write-Host ("==> {0}: {1} s" -f $Name, [int]$watch.Elapsed.TotalSeconds)
}

switch ($command) {
    "image" {
        & $engine build -t $image -f (Join-Path $root "tools/dev/Containerfile") (Join-Path $root "tools/dev")
        exit $LASTEXITCODE
    }
    "doctor" {
        & $engine --version
        & $engine image inspect $image *> $null
        if ($LASTEXITCODE -eq 0) { "image: $image"; Invoke-Inside @("doctor") }
        else { "image: missing (run .\tools\dev.ps1 image)" }
    }
    { $_ -in "test", "build", "preview", "lint", "legacy-test" } {
        Assert-Image
        Invoke-Timed $command (@($command) + $rest)
    }
    "deploy" {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Invoke-Deploy $rest
        Write-Host ("==> deploy: {0} s" -f [int]$watch.Elapsed.TotalSeconds)
    }
    "all" {
        Assert-Image
        Invoke-Timed "build" @("build")
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Invoke-Deploy $rest
        Write-Host ("==> deploy: {0} s" -f [int]$watch.Elapsed.TotalSeconds)
    }
    "shell" {
        Assert-Image
        Invoke-Inside @("shell")
    }
    { $_ -in "help", "-h", "--help" } {
        Get-Content (Join-Path $root "tools/dev.sh") | Select-Object -Skip 5 -First 20 |
            ForEach-Object { $_ -replace "^# ?", "" -replace "tools/dev.sh", ".\tools\dev.ps1" }
    }
    default { Fail "unknown command: $command (.\tools\dev.ps1 help)" }
}
