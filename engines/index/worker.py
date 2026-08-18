"""Isolated IndexTTS worker for gclone.

The currently runnable official release is IndexTTS2. IndexTTS 2.5 has a technical report and
samples, but no official runnable release as of the repository review captured by this project.
This adapter is intentionally isolated so the 2.5 runtime can replace it without changing the UI.
"""

from __future__ import annotations

import contextlib
import json
import sys
import traceback
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
RUNTIME = ROOT / "runtime"
CHECKPOINTS = RUNTIME / "checkpoints"
_model = None


def emit(event: str, *, terminal: bool = False, **payload: Any) -> None:
    sys.stdout.write(json.dumps({"event": event, "terminal": terminal, **payload}, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def capabilities() -> dict[str, Any]:
    return {
        "engine": "index",
        "display_name": "IndexTTS 2 (current runnable release)",
        "language": False,
        "reference_transcript": False,
        "x_vector_only": False,
        "emotion_text": True,
        "emotion_strength": True,
        "temperature": True,
        "top_p": True,
        "top_k": True,
        "repetition_penalty": True,
    }


def ensure_model():
    global _model
    if _model is not None:
        return _model
    if not RUNTIME.is_dir():
        raise RuntimeError("IndexTTS is not installed. Run engines\\index\\setup.ps1 first.")
    if not (CHECKPOINTS / "config.yaml").is_file():
        raise RuntimeError("IndexTTS model files are missing. Re-run engines\\index\\setup.ps1.")

    emit("status", message="Loading IndexTTS 2 in FP16…")
    try:
        sys.path.insert(0, str(RUNTIME))
        with contextlib.redirect_stdout(sys.stderr):
            from indextts.infer_v2 import IndexTTS2

            _model = IndexTTS2(
                cfg_path=str(CHECKPOINTS / "config.yaml"),
                model_dir=str(CHECKPOINTS),
                use_fp16=True,
                device="cuda:0",
                use_cuda_kernel=False,
                use_deepspeed=False,
            )
    except Exception as exc:
        raise RuntimeError("IndexTTS could not load: " + str(exc)) from exc
    return _model


def generate(request: dict[str, Any]) -> None:
    reference_audio = str(request.get("reference_audio", "")).strip()
    target_text = str(request.get("text", "")).strip()
    emotion_text = str(request.get("emotion_text", "")).strip()
    emotion_strength = max(0.0, min(1.0, float(request.get("emotion_strength", 0.6))))
    output_path = Path(str(request.get("output_path", "")))

    if not reference_audio or not Path(reference_audio).is_file():
        raise ValueError("The selected reference audio file does not exist.")
    if not target_text:
        raise ValueError("The text to speak is empty.")

    model = ensure_model()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    kwargs: dict[str, Any] = {}
    if "temperature" in request: kwargs["temperature"] = float(request["temperature"])
    if "top_p" in request: kwargs["top_p"] = float(request["top_p"])
    if "top_k" in request: kwargs["top_k"] = int(float(request["top_k"]))
    if "repetition_penalty" in request: kwargs["repetition_penalty"] = float(request["repetition_penalty"])

    emit("status", message="Preparing reference voice…")
    emit("status", message="Generating speech…")
    # IndexTTS performs its audio load/cut/resample inside infer(), so the source remains untouched
    # until this Generate request reaches the worker.
    with contextlib.redirect_stdout(sys.stderr):
        model.infer(
            spk_audio_prompt=reference_audio,
            text=target_text,
            output_path=str(output_path),
            use_emo_text=bool(emotion_text),
            emo_text=emotion_text or None,
            emo_alpha=emotion_strength,
            verbose=False,
            **kwargs,
        )

    if not output_path.is_file():
        raise RuntimeError("IndexTTS returned without creating an output WAV.")
    emit("result", terminal=True, output_path=str(output_path))


def unload() -> None:
    global _model
    _model = None
    try:
        with contextlib.redirect_stdout(sys.stderr):
            import gc
            import torch
            gc.collect()
            if torch.cuda.is_available(): torch.cuda.empty_cache()
    except Exception:
        pass


def handle(request: dict[str, Any]) -> bool:
    command = request.get("command")
    if command == "capabilities":
        emit("capabilities", terminal=True, **capabilities())
    elif command == "generate":
        generate(request)
    elif command == "unload":
        unload(); emit("unloaded", terminal=True)
    elif command == "shutdown":
        unload(); emit("shutdown", terminal=True); return False
    else:
        emit("error", terminal=True, message=f"Unknown backend command: {command!r}")
    return True


def main() -> int:
    for raw in sys.stdin:
        raw = raw.strip()
        if not raw: continue
        try:
            if not handle(json.loads(raw)): break
        except Exception as exc:
            traceback.print_exc(file=sys.stderr)
            emit("error", terminal=True, message=str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
