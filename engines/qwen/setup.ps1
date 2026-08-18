$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

. (Join-Path $PSScriptRoot "..\common\bootstrap-uv.ps1")

$gcloneRoot = Join-Path $env:LOCALAPPDATA "gclone"
$engineRoot = Join-Path $gcloneRoot "runtimes\qwen"
$venv = Join-Path $engineRoot ".venv"
$python = Join-Path $venv "Scripts\python.exe"
$marker = Join-Path $engineRoot ".ready-qwen-1.7b-v2"
$oldMarker = Join-Path $engineRoot ".ready-qwen-1.7b-v1"

New-Item -ItemType Directory -Force -Path $engineRoot | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $marker
Remove-Item -Force -ErrorAction SilentlyContinue $oldMarker

$uv = Initialize-GCloneUv -GCloneRoot $gcloneRoot

if (-not (Test-Path $python)) {
    Write-Output "GCLONE:Downloading private Python 3.12 runtime..."
    & $uv --color never venv $venv --python 3.12 --managed-python
    if ($LASTEXITCODE -ne 0) { throw "uv could not create the Qwen Python environment." }
}

# qwen-tts currently depends on torchaudio without selecting a CUDA wheel index. On a clean
# Windows environment that can resolve to CPU-only PyTorch. Install a known CUDA build first
# so qwen-tts reuses it instead of silently producing an environment that cannot run cuda:0.
Write-Output "GCLONE:Installing CUDA-enabled PyTorch for Qwen..."
& $uv --color never pip install --python $python --index-url "https://download.pytorch.org/whl/cu128" "torch==2.8.0" "torchaudio==2.8.0"
if ($LASTEXITCODE -ne 0) { throw "uv could not install the CUDA 12.8 PyTorch runtime for Qwen." }

Write-Output "GCLONE:Installing Qwen3-TTS engine packages..."
& $uv --color never pip install --python $python --upgrade "qwen-tts==0.1.1"
if ($LASTEXITCODE -ne 0) { throw "uv could not install qwen-tts." }

Write-Output "GCLONE:Verifying Qwen CUDA runtime..."
& $python -c "import torch, torchaudio, qwen_tts; assert torch.version.cuda is not None, 'PyTorch is CPU-only'; assert torch.cuda.is_available(), 'CUDA is unavailable to PyTorch'; print('CUDA OK:', torch.__version__, 'CUDA', torch.version.cuda, torch.cuda.get_device_name(0))"
if ($LASTEXITCODE -ne 0) {
    throw "Qwen's CUDA runtime did not pass verification. Make sure the NVIDIA driver is current and the GPU is available."
}

$env:HF_HOME = Join-Path $engineRoot "hf"
New-Item -ItemType Directory -Force -Path $env:HF_HOME | Out-Null

Write-Output "GCLONE:Downloading Qwen3-TTS 1.7B model files..."
& $python -c "from huggingface_hub import snapshot_download; snapshot_download('Qwen/Qwen3-TTS-12Hz-1.7B-Base')"
if ($LASTEXITCODE -ne 0) { throw "Qwen model download failed." }

Set-Content -Path $marker -Value "qwen-1.7b-v2" -Encoding ASCII
Write-Output "GCLONE:Qwen3-TTS is ready."
