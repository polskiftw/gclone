$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "Setting up isolated Qwen3-TTS environment..."
py -3.12 -m venv .venv
& .\.venv\Scripts\python.exe -m pip install --upgrade pip
& .\.venv\Scripts\python.exe -m pip install --upgrade qwen-tts

Write-Host ""
Write-Host "Qwen engine environment is ready."
Write-Host "The 1.7B model weights will download on the first Generate unless already cached by Hugging Face."
