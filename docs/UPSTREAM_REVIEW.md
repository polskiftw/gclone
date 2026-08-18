# Upstream integration and licensing review

Reviewed for the initial implementation on 2026-08-17 and updated for automatic provisioning / first-run hardening on 2026-08-18.

## Qwen3-TTS

Official upstream: `QwenLM/Qwen3-TTS`

Current findings:

- Repository/package code is Apache-2.0.
- The official package is `qwen-tts`.
- The official quickstart recommends a fresh Python 3.12 environment.
- The 12 Hz 1.7B Base model is the released voice-clone target used by gclone.
- The official clone API is `Qwen3TTSModel.generate_voice_clone(...)`.
- High-fidelity ICL clone mode uses reference audio + reference transcript.
- `x_vector_only_mode=True` permits speaker-embedding-only cloning without transcript, with reduced clone quality.
- Clone prompts can be precomputed with `create_voice_clone_prompt(...)` and reused, which is what the gclone worker does.
- Qwen accepts `language="Auto"` for language-adaptive generation.
- The current `qwen-tts` package depends on `torchaudio` but does not itself select a CUDA wheel index. Upstream users have reported clean Windows installs resolving to CPU-only PyTorch, which is incompatible with gclone's `cuda:0` backend.

Integration decision:

- Provision Qwen under `%LOCALAPPDATA%\gclone\runtimes\qwen`, not beside `gclone.exe`.
- Use an app-private Python 3.12 environment managed by gclone's app-local `uv` installation.
- Install the official PyTorch/torchaudio 2.8.0 CUDA 12.8 wheels before `qwen-tts==0.1.1`, then explicitly verify `torch.version.cuda` and `torch.cuda.is_available()` before writing the Ready marker. This prevents a syntactically successful but CPU-only Qwen installation from being treated as usable.
- Download the Qwen model during the first-run Install flow so Generate does not silently begin a multi-gigabyte model download after setup supposedly completed.
- Do not vendor Qwen source or model weights in the repository/build artifact.
- Use the public Python API instead of an upstream web UI.
- Use SDPA initially on Windows; FlashAttention remains optional rather than a required installation burden.

## IndexTTS-2.5

Official upstream: `index-tts/index-tts`

Re-reviewed on 2026-08-17 after the 2026-08-10 IndexTTS-2.5 release was identified.

Current findings:

- The official repository identifies **IndexTTS-2.5** as the latest release and ships runnable `indextts/infer_v2_5.py`.
- Official weights are published as `IndexTeam/IndexTTS-2.5` on Hugging Face/ModelScope.
- The package requires Python `>=3.10,<3.12`, uses PyTorch 2.8, and its Windows/Linux `uv` configuration targets CUDA 12.8 wheels.
- The 2.5 Python API supports speaker reference audio, five languages (Chinese, English, Japanese, Spanish, Arabic), emotion reference/vector/text paths, `emo_alpha`, and native `duration_factor` control from 0.5–2.0x duration.
- Text-driven emotion requires constructing the model with `use_qwen_emo=True`; gclone enables that component only when an emotion description is requested.
- Upstream caches speaker conditioning when the same reference clip is reused, which benefits repeated generations in one worker session.
- The repository license remains the custom **bilibili Model Use License Agreement**, not Apache-2.0. gclone therefore does not vendor or relicense Index source/model assets.

Automatic-provisioning decision:

- Use the reviewed official upstream source snapshot at commit `4f8792ff120cd3ea470dd511e997a17c86cddd10` for reproducible first-run installs rather than cloning whatever `main` happens to contain later.
- Download that source archive directly over HTTPS so system Git is not a user prerequisite.
- Store source, virtual environment, model checkpoints, Hugging Face cache, and readiness marker under `%LOCALAPPDATA%\gclone\runtimes\index`.
- Let app-local `uv` supply the compatible managed Python runtime and create the upstream project environment.
- Use `indextts.infer_v2_5.IndexTTS2` directly.
- Use BF16 on the CUDA backend and avoid DeepSpeed/custom CUDA kernels as hard Windows requirements in the first integration.
- Download only the official `IndexTeam/IndexTTS-2.5` checkpoint set for this adapter.
- Keep Index isolated from Qwen so their Python/PyTorch requirements cannot collide and switching engine families can terminate the old worker to release VRAM.

## Provisioning tooling boundary

Normal users are not expected to install Python, Git, `uv`, packages, or model weights themselves. `gclone.exe` invokes the bundled setup scripts as an implementation detail only after the user explicitly chooses Install/Repair from the first-Generate flow.

`uv` is bootstrapped into `%LOCALAPPDATA%\gclone\tools\uv` using Astral's official PowerShell installer with an unmanaged install directory. Its cache and managed Python installation directories are redirected into `%LOCALAPPDATA%\gclone\uv`, and it does not modify the user's PATH/shell profile.

The native installer runner places PowerShell and its descendants in a Windows Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Cancelling setup or closing gclone therefore terminates the provisioning process tree rather than only the parent shell.

The readiness markers are deliberately versioned. A future runtime layout/model integration change can bump the expected marker and cause gclone to offer Repair/Update without pretending an old installation is current.

## Wrapper license

No `LICENSE` file is being added yet. A wrapper license can be selected once the final redistribution/install boundary is settled. Qwen's Apache-2.0 terms and Index's separate custom model/source terms should remain clearly separated from gclone wrapper code and downloaded assets.
