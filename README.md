# gclone

`gclone` is a native Windows frontend for local voice cloning / text-to-speech engines. The desktop UI stays independent from each Python/CUDA stack, so model families can use isolated environments without fighting over one shared installation.

The current vertical slice includes the native shell, model-specific controls, Qwen3-TTS voice cloning, IndexTTS-2.5 voice cloning, automatic first-run engine provisioning, temporary session cleanup, playback/seek, WAV export, and Windows-native MP3 export.

## Normal workflow

There is no manual Python, Git, `uv`, or model setup step for normal use.

1. Open `gclone.exe`.
2. Pick or drag in a voice sample.
3. Enter the text to speak.
4. Choose Qwen3-TTS or IndexTTS-2.5.
5. Click **Generate**.
6. If that engine is not installed yet, gclone offers an **Install** action, provisions it in the background, verifies it, and then automatically continues the generation you originally requested.
7. Listen in gclone and export WAV or MP3 if you want to keep the result.

Selecting a model is intentionally cheap: it does not start Python, load a model, or install anything. Selecting an audio file also does **not** decode, transcode, normalize, resample, or modify it. Expensive work begins only after Generate.

## Automatic provisioning

Engine runtimes and model data are user-local rather than stored beside the executable:

```text
%LOCALAPPDATA%\gclone\
  runtimes\qwen\
  runtimes\index\
  tools\uv\
  uv\
  cache\
```

On first use, gclone bootstraps its own private `uv` executable and lets `uv` obtain the required managed Python versions. Nothing is added to the user's PATH or shell profile, and gclone does not require a pre-existing system Python installation.

Qwen provisioning creates a private Python 3.12 environment, installs a pinned CUDA 12.8 PyTorch/torchaudio pair before the official `qwen-tts` package, verifies that PyTorch can actually see an NVIDIA CUDA device, and downloads `Qwen/Qwen3-TTS-12Hz-1.7B-Base` into gclone's local Hugging Face cache. A CPU-only PyTorch environment is not accepted as Ready.

Index provisioning downloads the reviewed official IndexTTS source snapshot, creates a private upstream-compatible `uv` environment, and downloads the official `IndexTeam/IndexTTS-2.5` checkpoints. System Git is not required.

Each engine has a versioned ready marker. Missing markers, incomplete runtime files, or future marker-version changes are treated as repair/update states. Re-running provisioning is safe: package/model caches are reused where possible, so an interrupted download can be retried instead of intentionally starting from zero.

The native provisioner runs setup inside a Windows Job Object with kill-on-close semantics. Cancelling or closing gclone during setup terminates the provisioning process tree instead of intentionally leaving hidden `uv`/Python download children behind.

The bundled `setup.ps1` files remain in the repository as implementation/recovery tools, but users are not expected to run them manually.

## Backends

### Qwen3-TTS 12Hz 1.7B Base

`gclone` uses the official `Qwen3TTSModel` voice-clone API. High-fidelity mode uses the reference sample plus its transcript. Advanced speaker-embedding-only mode can omit the transcript at reduced cloning fidelity.

The model is loaded lazily only for generation. A clone prompt is cached inside the worker and reused while the same reference sample/settings remain active.

### IndexTTS-2.5

The Index backend uses the official runnable IndexTTS-2.5 API and `IndexTeam/IndexTTS-2.5` checkpoints through `indextts.infer_v2_5.IndexTTS2` rather than the upstream Gradio UI.

The native UI exposes only relevant Index controls: English/Chinese/Japanese/Spanish/Arabic, text-driven emotion + strength, `duration_factor`, and supported advanced generation parameters. Upstream speaker conditioning is reused for repeated generations with the same reference clip. The optional QwenEmotion component is loaded only when an emotion description is supplied.

## Session and export behavior

Generated WAVs live under a per-process session directory in `%LOCALAPPDATA%\gclone\cache`. Clean shutdown removes the current session; abandoned crash sessions older than 48 hours are cleaned later.

Export supports:

- WAV — direct persistent copy
- MP3 — Windows Media Foundation encoding at 192 kbps

Nothing is permanently saved by gclone unless Export is used. The original reference audio is always read-only.

## Build

Requirements for building gclone itself:

- Windows 11 (Windows 10 may also work, but is not the current target)
- Visual Studio 2022+ with Desktop development with C++
- CMake 3.24+

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The result is `build\Release\gclone.exe`; CMake copies the worker/installer support tree beside it. The executable uses the static MSVC runtime and the frontend has no third-party GUI dependency.

## Architecture

```text
gclone.exe
  ├─ automatic provisioner -> %LOCALAPPDATA%\gclone\...
  ├─ engines/qwen/launch.cmd  -> isolated Qwen worker
  └─ engines/index/launch.cmd -> isolated IndexTTS-2.5 worker
```

Workers communicate with newline-delimited JSON over redirected stdin/stdout. Switching model families terminates the previous worker, giving gclone a hard process boundary for Python dependencies and GPU-memory release.

## License

`gclone` is licensed under the PolyForm Noncommercial License 1.0.0.

`gclone` installs [Qwen3-TTS 12Hz 1.7B Base](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-1.7B-Base) and [IndexTTS-2.5](https://huggingface.co/IndexTeam/IndexTTS-2.5). Those models and their associated third-party software are governed by their own licenses.
