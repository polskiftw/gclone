$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

. (Join-Path $PSScriptRoot "..\common\bootstrap-uv.ps1")

$gcloneRoot = Join-Path $env:LOCALAPPDATA "gclone"
$engineRoot = Join-Path $gcloneRoot "runtimes\qwen"
$venv = Join-Path $engineRoot ".venv"
$python = Join-Path $venv "Scripts\python.exe"
$marker = Join-Path $engineRoot ".ready-qwen-1.7b-v1"

New-Item -ItemType Directory -Force -Path $engineRoot | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $marker

$uv = Initialize-GCloneUv -GCloneRoot $gcloneRoot

if (-not (Test-Path $python)) {
    Write-Output "GCLONE:Downloading private Python 3.12 runtime..."
    & $uv --color never venv $venv --python 3.12 --managed-python
    if ($LASTEXITCODE -ne 0) { throw "uv could not create the Qwen Python environment." }
}

Write-Output "GCLONE:Installing Qwen3-TTS engine packages..."
& $uv --color never pip install --python $python --upgrade qwen-tts
if ($LASTEXITCODE -ne 0) { throw "uv could not install qwen-tts." }

$env:HF_HOME = Join-Path $engineRoot "hf"
New-Item -ItemType Directory -Force -Path $env:HF_HOME | Out-Null

Write-Output "GCLONE:Downloading Qwen3-TTS 1.7B model files..."
& $python -c "from huggingface_hub import snapshot_download; snapshot_download('Qwen/Qwen3-TTS-12Hz-1.7B-Base')"
if ($LASTEXITCODE -ne 0) { throw "Qwen model download failed." }

Set-Content -Path $marker -Value "qwen-1.7b-v1" -Encoding ASCII
Write-Output "GCLONE:Qwen3-TTS is ready."
