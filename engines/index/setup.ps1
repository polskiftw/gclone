$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to install the official IndexTTS runtime."
}
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    py -3.11 -m pip install --user --upgrade uv
    $env:Path = "$env:USERPROFILE\.local\bin;$env:APPDATA\Python\Python311\Scripts;$env:Path"
}

if (-not (Test-Path runtime\.git)) {
    git clone --depth 1 https://github.com/index-tts/index-tts.git runtime
}

Write-Host "Creating the isolated IndexTTS environment using the upstream-supported uv workflow..."
uv sync --project runtime

if (-not (Test-Path runtime\checkpoints\config.yaml)) {
    Write-Host "Downloading official IndexTTS2 model files..."
    uvx --from "huggingface-hub[cli,hf_xet]" hf download IndexTeam/IndexTTS-2 --local-dir runtime/checkpoints
}

Write-Host ""
Write-Host "IndexTTS 2 runtime is ready."
Write-Host "Note: the published IndexTTS 2.5 technical report does not yet have an official runnable release."
