$ErrorActionPreference = "Stop"

function Initialize-GCloneUv {
    param([Parameter(Mandatory = $true)][string]$GCloneRoot)

    $toolDir = Join-Path $GCloneRoot "tools\uv"
    $uvExe = Join-Path $toolDir "uv.exe"
    New-Item -ItemType Directory -Force -Path $toolDir | Out-Null

    if (-not (Test-Path $uvExe)) {
        Write-Output "GCLONE:Installing gclone's local runtime manager..."
        $env:UV_UNMANAGED_INSTALL = $toolDir
        $env:UV_NO_MODIFY_PATH = "1"
        $installer = Invoke-RestMethod "https://astral.sh/uv/install.ps1"
        Invoke-Expression $installer
    }

    if (-not (Test-Path $uvExe)) {
        throw "uv installation completed without creating $uvExe"
    }

    $env:UV_CACHE_DIR = Join-Path $GCloneRoot "uv\cache"
    $env:UV_PYTHON_INSTALL_DIR = Join-Path $GCloneRoot "uv\python"
    $env:UV_PYTHON_NO_REGISTRY = "1"
    $env:UV_PYTHON_PREFERENCE = "only-managed"

    New-Item -ItemType Directory -Force -Path $env:UV_CACHE_DIR | Out-Null
    New-Item -ItemType Directory -Force -Path $env:UV_PYTHON_INSTALL_DIR | Out-Null

    return $uvExe
}
