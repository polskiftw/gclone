$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
Set-Location $PSScriptRoot

. (Join-Path $PSScriptRoot "..\common\bootstrap-uv.ps1")

$gcloneRoot = Join-Path $env:LOCALAPPDATA "gclone"
$engineRoot = Join-Path $gcloneRoot "runtimes\qwen"
$venv = Join-Path $engineRoot ".venv"
$python = Join-Path $venv "Scripts\python.exe"
$marker = Join-Path $engineRoot ".ready-qwen-1.7b-v3"
$oldMarkerV2 = Join-Path $engineRoot ".ready-qwen-1.7b-v2"
$oldMarkerV1 = Join-Path $engineRoot ".ready-qwen-1.7b-v1"

New-Item -ItemType Directory -Force -Path $engineRoot | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $marker
Remove-Item -Force -ErrorAction SilentlyContinue $oldMarkerV2
Remove-Item -Force -ErrorAction SilentlyContinue $oldMarkerV1

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
& $uv --color never pip install --python $python --upgrade "qwen-tts==0.1.1" "pytz>=2025.1"
if ($LASTEXITCODE -ne 0) { throw "uv could not install qwen-tts and its Windows compatibility dependencies." }

# qwen-tts 0.1.1 omits pytz from its declared dependencies even though a Windows install can
# need it through the imported runtime stack. Keep the explicit install above until upstream
# publishes corrected package metadata.
Write-Output "GCLONE:Verifying Qwen CUDA runtime and imports..."
& $python -c "import pytz, torch, torchaudio; from qwen_tts import Qwen3TTSModel; assert torch.version.cuda is not None, 'PyTorch is CPU-only'; assert torch.cuda.is_available(), 'CUDA is unavailable to PyTorch'; print('Qwen import OK; CUDA:', torch.__version__, 'CUDA', torch.version.cuda, torch.cuda.get_device_name(0))"
if ($LASTEXITCODE -ne 0) {
    throw "Qwen's Python/CUDA runtime did not pass verification. See the installer log for the underlying import or CUDA error."
}

$env:HF_HOME = Join-Path $engineRoot "hf"
$env:HF_HUB_DISABLE_PROGRESS_BARS = "1"
New-Item -ItemType Directory -Force -Path $env:HF_HOME | Out-Null

Write-Output "GCLONE:Downloading Qwen3-TTS 1.7B model files..."
& $python -c "from huggingface_hub import snapshot_download; snapshot_download('Qwen/Qwen3-TTS-12Hz-1.7B-Base')"
if ($LASTEXITCODE -ne 0) { throw "Qwen model download failed." }

Set-Content -Path $marker -Value "qwen-1.7b-v3" -Encoding ASCII
Write-Output "GCLONE:Qwen3-TTS is ready."
