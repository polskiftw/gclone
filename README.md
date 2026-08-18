# gclone

`gclone` is a small native Windows frontend for local voice-cloning / text-to-speech engines. It keeps the desktop UI independent from each Python/CUDA stack, so model families can use isolated environments instead of fighting over one shared PyTorch install.

This repository is at the first vertical-slice stage. The native shell, dynamic capability-driven controls, Qwen3-TTS voice-clone adapter, IndexTTS-2.5 adapter, temporary session cache, WAV playback, WAV export, and Windows-native MP3 export are implemented.

## Current backend status

### Qwen3-TTS 12Hz 1.7B Base

This is the primary first working backend. `gclone` uses the official `Qwen3TTSModel` voice-clone API with a reference sample and, by default, its transcript. Advanced speaker-embedding-only mode can skip the transcript at reduced cloning fidelity.

The model is loaded lazily only when Generate is clicked. A clone prompt is cached inside the worker and reused when the same reference sample/settings are used again.

### IndexTTS-2.5

The Index backend now uses the official runnable **IndexTTS-2.5** release and official `IndexTeam/IndexTTS-2.5` checkpoints. The adapter calls `indextts.infer_v2_5.IndexTTS2` directly; it does not use the upstream Gradio UI.

For IndexTTS-2.5 the native UI exposes only applicable controls: its five supported languages, text-driven emotion + strength, native `duration_factor` speed control, and supported advanced generation parameters. Speaker conditioning is cached by upstream when the same reference clip is reused. The optional QwenEmotion component is loaded only when an emotion description is actually supplied.

## UI behavior

The common surface stays small:

- voice sample picker / drag-and-drop
- text box
- model dropdown
- only the controls supported by the selected backend
- Generate
- playback + seek
- Export…
- collapsed Advanced controls

Selecting an audio file does **not** decode, transcode, normalize, resample, or load a model. Reference-audio processing begins only after Generate is clicked. The original source file is always read-only.

Generated WAVs live under a per-process session cache in `%LOCALAPPDATA%\gclone\cache`. Clean shutdown removes the current session. Old crash leftovers are cleaned after 48 hours.

## Build

Requirements:

- Windows 11 (Windows 10 may also work, but is not the current target)
- Visual Studio 2022 with Desktop development with C++
- CMake 3.24+

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The result is `build\Release\gclone.exe`. CMake copies the engine launchers/workers beside it automatically.

The executable uses the static MSVC runtime. The UI itself has no third-party GUI dependency.

## Set up Qwen3-TTS

Qwen is intentionally installed into its own Python 3.12 environment:

```powershell
powershell -ExecutionPolicy Bypass -File .\build\Release\engines\qwen\setup.ps1
```

The script installs the official `qwen-tts` package. Model weights are downloaded by the official package/Hugging Face path on first model load if they are not already cached.

`gclone` uses PyTorch SDPA by default instead of making FlashAttention a hard Windows installation dependency.

## Set up IndexTTS-2.5 (optional)

Index uses its own upstream-supported `uv` environment and a different Python compatibility range:

```powershell
powershell -ExecutionPolicy Bypass -File .\build\Release\engines\index\setup.ps1
```

The script clones/updates the official IndexTTS repository into the ignored local runtime directory, runs its `uv` environment workflow, and downloads the official `IndexTeam/IndexTTS-2.5` checkpoints. The upstream source/model files are **not** vendored into this repository.

## Export

The generated session artifact is WAV. Export currently supports:

- WAV — direct persistent copy
- MP3 — transcoded through Windows Media Foundation at 192 kbps

Nothing is permanently saved unless Export… is used.

## Architecture

```text
gclone.exe
  ├─ engines/qwen/launch.cmd -> isolated Qwen Python worker
  └─ engines/index/launch.cmd -> isolated Index runtime worker
```

Workers communicate with newline-delimited JSON over redirected stdin/stdout. Each worker advertises capabilities, and the frontend renders controls from those capabilities instead of assuming every TTS engine has equivalent knobs.

See `engines/PROTOCOL.md` and `docs/UPSTREAM_REVIEW.md`.

## License status

No wrapper license has been selected yet. This is intentional while the upstream boundary is being finalized. See `docs/UPSTREAM_REVIEW.md`.
