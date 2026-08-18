"""Isolated Qwen3-TTS worker for gclone.

Protocol: newline-delimited JSON on stdin/stdout. Any upstream/library chatter is redirected
away from stdout so the native frontend sees protocol messages only.
"""

from __future__ import annotations

import contextlib
import json
import os
import sys
import traceback
from pathlib import Path
from typing import Any

MODEL_ID = "Qwen/Qwen3-TTS-12Hz-1.7B-Base"
_model = None
_prompt_cache_key: tuple[str, str, bool] | None = None
_prompt_cache = None


def emit(event: str, *, terminal: bool = False, **payload: Any) -> None:
    message = {"event": event, "terminal": terminal, **payload}
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def capabilities() -> dict[str, Any]:
    return {
        "engine": "qwen",
        "display_name": "Qwen3-TTS 12Hz 1.7B Base",
        "language": True,
        "reference_transcript": True,
        "x_vector_only": True,
        "emotion_text": False,
        "emotion_strength": False,
        "temperature": True,
        "top_p": True,
        "top_k": True,
        "repetition_penalty": True,
    }


def ensure_model():
    global _model
    if _model is not None:
        return _model

    emit("status", message="Loading Qwen3-TTS 1.7B model…")
    try:
        with contextlib.redirect_stdout(sys.stderr):
            import torch
            from qwen_tts import Qwen3TTSModel

            _model = Qwen3TTSModel.from_pretrained(
                MODEL_ID,
                device_map="cuda:0",
                dtype=torch.bfloat16,
                attn_implementation="sdpa",
            )
    except Exception as exc:
        raise RuntimeError(
            "Qwen3-TTS could not load. Run engines\\qwen\\setup.ps1, then make sure the NVIDIA GPU "
            "and CUDA-enabled PyTorch environment are available. Details: " + str(exc)
        ) from exc
    return _model


def numeric_kwargs(request: dict[str, Any]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key in ("temperature", "top_p", "repetition_penalty"):
        if key in request:
            out[key] = float(request[key])
    if "top_k" in request:
        out["top_k"] = int(float(request["top_k"]))
    return out


def generate(request: dict[str, Any]) -> None:
    global _prompt_cache_key, _prompt_cache

    reference_audio = str(request.get("reference_audio", "")).strip()
    reference_transcript = str(request.get("reference_transcript", ""))
    target_text = str(request.get("text", "")).strip()
    output_path = Path(str(request.get("output_path", "")))
    language = str(request.get("language") or "Auto")
    x_vector_only = bool(request.get("x_vector_only", False))

    if not reference_audio or not Path(reference_audio).is_file():
        raise ValueError("The selected reference audio file does not exist.")
    if not target_text:
        raise ValueError("The text to speak is empty.")
    if not output_path:
        raise ValueError("No temporary output path was provided.")
    if not x_vector_only and not reference_transcript.strip():
        raise ValueError("Qwen high-fidelity clone mode requires the reference transcript.")

    model = ensure_model()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # IMPORTANT: the source file is first decoded/touched here, after Generate was clicked.
    # Qwen's official prompt builder performs its required decode/resample and keeps the source read-only.
    prompt_key = (os.path.abspath(reference_audio), reference_transcript, x_vector_only)
    if _prompt_cache_key != prompt_key or _prompt_cache is None:
        emit("status", message="Preparing reference voice…")
        with contextlib.redirect_stdout(sys.stderr):
            _prompt_cache = model.create_voice_clone_prompt(
                ref_audio=reference_audio,
                ref_text=None if x_vector_only else reference_transcript,
                x_vector_only_mode=x_vector_only,
            )
        _prompt_cache_key = prompt_key

    emit("status", message="Generating speech…")
    with contextlib.redirect_stdout(sys.stderr):
        import soundfile as sf

        wavs, sample_rate = model.generate_voice_clone(
            text=target_text,
            language=language,
            voice_clone_prompt=_prompt_cache,
            **numeric_kwargs(request),
        )
        sf.write(str(output_path), wavs[0], sample_rate, subtype="PCM_16")

    emit("result", terminal=True, output_path=str(output_path), sample_rate=int(sample_rate))


def unload() -> None:
    global _model, _prompt_cache_key, _prompt_cache
    _model = None
    _prompt_cache = None
    _prompt_cache_key = None
    try:
        with contextlib.redirect_stdout(sys.stderr):
            import gc
            import torch

            gc.collect()
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
    except Exception:
        pass


def handle(request: dict[str, Any]) -> bool:
    command = request.get("command")
    if command == "capabilities":
        emit("capabilities", terminal=True, **capabilities())
    elif command == "generate":
        generate(request)
    elif command == "unload":
        unload()
        emit("unloaded", terminal=True)
    elif command == "shutdown":
        unload()
        emit("shutdown", terminal=True)
        return False
    else:
        emit("error", terminal=True, message=f"Unknown backend command: {command!r}")
    return True


def main() -> int:
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            request = json.loads(raw)
            if not handle(request):
                break
        except Exception as exc:
            traceback.print_exc(file=sys.stderr)
            emit("error", terminal=True, message=str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
