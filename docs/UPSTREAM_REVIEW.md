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

## IndexTTS-2.5

Official upstream: `index-tts/index-tts`

Re-reviewed on 2026-08-17 after the 2026-08-10 IndexTTS-2.5 release was identified.

Current findings:

- The official repository now identifies **IndexTTS-2.5** as the latest release and ships runnable `indextts/infer_v2_5.py`.
- Official weights are published as `IndexTeam/IndexTTS-2.5` on Hugging Face/ModelScope and the upstream README documents downloading them into `checkpoints`.
- The package requires Python `>=3.10,<3.12`, uses PyTorch 2.8, and its Windows/Linux `uv` configuration targets CUDA 12.8 wheels.
- The 2.5 Python API supports speaker reference audio, five languages (Chinese, English, Japanese, Spanish, Arabic), emotion reference/vector/text paths, `emo_alpha`, and native `duration_factor` control from 0.5–2.0x duration.
- Text-driven emotion requires constructing the model with `use_qwen_emo=True`; gclone therefore enables that component only when an emotion description is requested.
- Upstream caches speaker conditioning when the same reference clip is reused, which benefits repeated generations in one gclone worker session.
- The repository license remains the custom **bilibili Model Use License Agreement**, not Apache-2.0. gclone therefore does not vendor or relicense Index source/model assets; the setup script fetches them into an ignored local runtime directory.

Integration decision:

- Use `indextts.infer_v2_5.IndexTTS2` directly.
- Use BF16 on the CUDA backend and avoid DeepSpeed/custom CUDA kernels as hard Windows requirements in the first integration.
- Download only the official `IndexTeam/IndexTTS-2.5` checkpoint set for this adapter.
- Keep Index isolated from Qwen so their Python/PyTorch requirements cannot collide and switching engine families can terminate the old worker to release VRAM.

## Wrapper license

No `LICENSE` file is being added in this first implementation commit. A permissive wrapper license can be chosen later once the final redistribution/install boundary is settled. Qwen's Apache-2.0 terms do not prevent a permissive wrapper, but Index's separate custom terms should remain clearly separated from wrapper code and assets.
