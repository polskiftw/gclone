$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to install the official IndexTTS 2.5 runtime."
}
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    py -3.11 -m pip install --user --upgrade uv
    $env:Path = "$env:USERPROFILE\.local\bin;$env:APPDATA\Python\Python311\Scripts;$env:Path"
}

if (-not (Test-Path runtime\.git)) {
    git clone --depth 1 https://github.com/index-tts/index-tts.git runtime
} else {
    Write-Host "Updating the official IndexTTS runtime..."
    git -C runtime pull --ff-only
}

Write-Host "Creating the isolated IndexTTS 2.5 environment using the upstream uv workflow..."
uv sync --project runtime

Write-Host "Reconciling official IndexTTS 2.5 model files..."
# Always target the official 2.5 snapshot. huggingface-hub reuses its cache, so already-current
# files are not needlessly downloaded, and an old pre-2.5 gclone checkpoint directory is upgraded.
uvx --from "huggingface-hub[cli,hf_xet]" hf download IndexTeam/IndexTTS-2.5 --local-dir runtime/checkpoints

Write-Host ""
Write-Host "IndexTTS 2.5 runtime is ready."
Write-Host "gclone will load the main model lazily when Generate is clicked."
