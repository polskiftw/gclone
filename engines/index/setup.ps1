$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

. (Join-Path $PSScriptRoot "..\common\bootstrap-uv.ps1")

$gcloneRoot = Join-Path $env:LOCALAPPDATA "gclone"
$engineRoot = Join-Path $gcloneRoot "runtimes\index"
$sourceRoot = Join-Path $engineRoot "source"
$venv = Join-Path $engineRoot ".venv"
$python = Join-Path $venv "Scripts\python.exe"
$checkpoints = Join-Path $engineRoot "checkpoints"
$marker = Join-Path $engineRoot ".ready-index-2.5-v1"
$upstreamCommit = "4f8792ff120cd3ea470dd511e997a17c86cddd10"
$sourceMarker = Join-Path $sourceRoot (".gclone-upstream-" + $upstreamCommit)

New-Item -ItemType Directory -Force -Path $engineRoot | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue $marker

$uv = Initialize-GCloneUv -GCloneRoot $gcloneRoot

if (-not (Test-Path $sourceMarker) -or -not (Test-Path (Join-Path $sourceRoot "indextts\infer_v2_5.py"))) {
    Write-Output "GCLONE:Downloading official IndexTTS 2.5 runtime source..."
    $tempRoot = Join-Path $env:TEMP ("gclone-index-" + [Guid]::NewGuid().ToString("N"))
    $zipPath = Join-Path $tempRoot "index.zip"
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    try {
        Invoke-WebRequest -UseBasicParsing -Uri ("https://github.com/index-tts/index-tts/archive/" + $upstreamCommit + ".zip") -OutFile $zipPath
        Expand-Archive -Path $zipPath -DestinationPath $tempRoot -Force
        $extracted = Get-ChildItem -Path $tempRoot -Directory | Where-Object { $_.Name -like "index-tts-*" } | Select-Object -First 1
        if (-not $extracted) { throw "Could not find the extracted IndexTTS source tree." }
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $sourceRoot
        New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
        Copy-Item -Path (Join-Path $extracted.FullName "*") -Destination $sourceRoot -Recurse -Force
        Set-Content -Path $sourceMarker -Value $upstreamCommit -Encoding ASCII
    }
    finally {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $tempRoot
    }
}

Write-Output "GCLONE:Creating private IndexTTS Python environment..."
$env:UV_PROJECT_ENVIRONMENT = $venv
& $uv --color never sync --project $sourceRoot
if ($LASTEXITCODE -ne 0) { throw "uv could not create the IndexTTS environment." }

if (-not (Test-Path $python)) { throw "IndexTTS environment is missing its Python executable." }

$env:HF_HOME = Join-Path $engineRoot "hf"
$env:HF_HUB_DISABLE_PROGRESS_BARS = "1"
New-Item -ItemType Directory -Force -Path $env:HF_HOME | Out-Null
New-Item -ItemType Directory -Force -Path $checkpoints | Out-Null

Write-Output "GCLONE:Downloading official IndexTTS 2.5 model files..."
$uvx = Join-Path (Split-Path $uv -Parent) "uvx.exe"
if (-not (Test-Path $uvx)) { throw "gclone's local uvx executable is missing." }
& $uvx --from "huggingface-hub[cli,hf_xet]" hf download IndexTeam/IndexTTS-2.5 --local-dir $checkpoints
if ($LASTEXITCODE -ne 0) { throw "IndexTTS model download failed." }

if (-not (Test-Path (Join-Path $checkpoints "config.yaml"))) { throw "IndexTTS checkpoint verification failed." }
Set-Content -Path $marker -Value "index-2.5-v1" -Encoding ASCII
Write-Output "GCLONE:IndexTTS 2.5 is ready."
