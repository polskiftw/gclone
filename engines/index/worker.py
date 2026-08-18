"""Isolated IndexTTS-2.5 worker for gclone.

The official IndexTTS source/runtime is provisioned by gclone under Local AppData.
Stdout is reserved for gclone's newline-delimited JSON protocol; upstream output goes to stderr.
"""

from __future__ import annotations

import contextlib
import json
import os
import sys
import traceback
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
ENGINE_ROOT = Path(os.environ.get("GCLONE_INDEX_ROOT", str(ROOT / "runtime")))
RUNTIME = ENGINE_ROOT / "source"
CHECKPOINTS = ENGINE_ROOT / "checkpoints"
_model = None
_model_has_qwen_emo = False

LANGUAGES = {
    "English": "EN",
    "Chinese": "ZH",
    "Japanese": "JA",
    "Spanish": "ES",
    "Arabic": "AR",
}


def emit(event: str, *, terminal: bool = False, **payload: Any) -> None:
    sys.stdout.write(json.dumps({"event": event, "terminal": terminal, **payload}, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def capabilities() -> dict[str, Any]:
    return {
        "engine": "index",
        "display_name": "IndexTTS 2.5",
        "language": True,
        "language_options": "|".join(LANGUAGES),
        "reference_transcript": False,
        "x_vector_only": False,
        "emotion_text": True,
        "emotion_strength": True,
        "duration_factor": True,
        "temperature": True,
        "top_p": True,
        "top_k": True,
        "repetition_penalty": True,
    }


def _release_model() -> None:
    global _model, _model_has_qwen_emo
    _model = None
    _model_has_qwen_emo = False
    try:
        with contextlib.redirect_stdout(sys.stderr):
            import gc
            import torch

            gc.collect()
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
    except Exception:
        pass


def ensure_model(*, require_qwen_emo: bool = False):
    global _model, _model_has_qwen_emo
    if _model is not None and (not require_qwen_emo or _model_has_qwen_emo):
        return _model

    if _model is not None:
        emit("status", message="Reloading IndexTTS 2.5 with text-emotion guidance…")
        _release_model()

    if not RUNTIME.is_dir() or not (RUNTIME / "indextts" / "infer_v2_5.py").is_file():
        raise RuntimeError("The IndexTTS 2.5 runtime is unavailable. Use gclone's automatic Install/Repair flow.")
    if not (CHECKPOINTS / "config.yaml").is_file():
        raise RuntimeError("The IndexTTS 2.5 model files are unavailable. Use gclone's automatic Install/Repair flow.")

    emit("status", message="Loading IndexTTS 2.5 in BF16…")
    try:
        sys.path.insert(0, str(RUNTIME))
        with contextlib.redirect_stdout(sys.stderr):
            from indextts.infer_v2_5 import IndexTTS2

            _model = IndexTTS2(
                cfg_path=str(CHECKPOINTS / "config.yaml"),
                model_dir=str(CHECKPOINTS),
                use_bf16=True,
                device="cuda:0",
                use_cuda_kernel=False,
                use_deepspeed=False,
                use_accel=False,
                use_torch_compile=False,
                use_qwen_emo=require_qwen_emo,
            )
            _model_has_qwen_emo = require_qwen_emo
    except Exception as exc:
        _release_model()
        raise RuntimeError(
            "IndexTTS 2.5 could not load. gclone can repair the engine files, but the NVIDIA driver "
            "and CUDA-compatible GPU stack must also be available. Details: " + str(exc)
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
    reference_audio = str(request.get("reference_audio", "")).strip()
    target_text = str(request.get("text", "")).strip()
    language_name = str(request.get("language") or "English")
    emotion_text = str(request.get("emotion_text", "")).strip()
    emotion_strength = max(0.0, min(1.0, float(request.get("emotion_strength", 0.6))))
    duration_factor = max(0.5, min(2.0, float(request.get("duration_factor", 1.0))))
    output_path_text = str(request.get("output_path", "")).strip()
    output_path = Path(output_path_text) if output_path_text else None

    if not reference_audio or not Path(reference_audio).is_file():
        raise ValueError("The selected reference audio file does not exist.")
    if not target_text:
        raise ValueError("The text to speak is empty.")
    if output_path is None:
        raise ValueError("No temporary output path was provided.")
    if language_name not in LANGUAGES:
        raise ValueError(f"Unsupported IndexTTS 2.5 language: {language_name}")

    model = ensure_model(require_qwen_emo=bool(emotion_text))
    output_path.parent.mkdir(parents=True, exist_ok=True)

    emit("status", message="Preparing reference voice…")
    emit("status", message="Generating speech…")
    with contextlib.redirect_stdout(sys.stderr):
        model.infer(
            spk_audio_prompt=reference_audio,
            text=target_text,
            lang=LANGUAGES[language_name],
            output_path=str(output_path),
            use_emo_text=bool(emotion_text),
            emo_text=emotion_text or None,
            emo_alpha=emotion_strength,
            duration_factor=duration_factor,
            verbose=False,
            **numeric_kwargs(request),
        )

    if not output_path.is_file():
        raise RuntimeError("IndexTTS 2.5 returned without creating an output WAV.")
    emit("result", terminal=True, output_path=str(output_path))


def unload() -> None:
    _release_model()


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
            if not handle(json.loads(raw)):
                break
        except Exception as exc:
            traceback.print_exc(file=sys.stderr)
            emit("error", terminal=True, message=str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
