# Upstream integration and licensing review

Reviewed for the initial implementation on 2026-08-17.

## Qwen3-TTS

Official upstream: `QwenLM/Qwen3-TTS`

Current findings:

- Repository/package code is Apache-2.0.
- The official package is `qwen-tts` (current project metadata observed at version 0.1.1).
- Current package metadata supports Python 3.9+; the official quickstart recommends a fresh Python 3.12 environment.
- The 12 Hz 1.7B Base model is the high-quality released voice-clone target.
- The official clone API is `Qwen3TTSModel.generate_voice_clone(...)`.
- High-fidelity ICL clone mode uses reference audio + reference transcript.
- `x_vector_only_mode=True` permits speaker-embedding-only cloning without transcript, with reduced clone quality.
- Clone prompts can be precomputed with `create_voice_clone_prompt(...)` and reused, which is what the gclone worker does.
- Qwen accepts `language="Auto"` for language-adaptive generation.

Integration decision:

- Install Qwen into `engines/qwen/.venv`; do not mix it with Index.
- Do not vendor Qwen source or model weights.
- Use the public Python API instead of the upstream Gradio UI.
- Use SDPA initially on Windows; FlashAttention remains an optional future optimization rather than a required installation burden.

## IndexTTS

Official upstream: `index-tts/index-tts`

Current findings:

- The runnable repository is IndexTTS2.
- The package currently requires Python `>=3.10,<3.12`, uses PyTorch 2.8, and its Windows/Linux `uv` configuration targets CUDA 12.8 wheels.
- Upstream explicitly supports/recommends `uv` rather than a generic pip/conda installation.
- The current `IndexTTS2.infer(...)` API supports speaker reference audio, emotion reference audio, emotion vectors, emotion text, emotion strength (`emo_alpha`), randomness, segmentation, and generation parameters such as top-p/top-k/temperature/beams/repetition control.
- The model internally loads/resamples reference audio during inference, so gclone can preserve the invariant that file selection itself is non-destructive and cheap.
- The current repository license is the custom **bilibili Model Use License Agreement**, not Apache-2.0. It defines the covered "Model" broadly enough to include published model weights and final code, and includes a separate-license threshold for very large organizations (over 100M MAU or RMB 1B annual revenue).
- Because of that custom license, gclone does not vendor or relicense Index source/model assets. The optional setup script fetches the official upstream runtime into a local ignored directory.

### IndexTTS 2.5 release boundary

IndexTTS 2.5 currently has an official technical report/project page describing the newer architecture and samples, but the official runnable GitHub repository/checkpoints remain IndexTTS2. The UI therefore does **not** falsely call the current engine "2.5".

When official runnable 2.5 code/weights appear, re-review its exact license and replace the isolated Index runtime adapter.

## Wrapper license

No `LICENSE` file is being added in this first implementation commit. A permissive wrapper license can be chosen later once the final redistribution/install boundary is settled. Qwen's Apache-2.0 terms do not prevent a permissive wrapper, but Index's separate custom terms should remain clearly separated from wrapper code and assets.
