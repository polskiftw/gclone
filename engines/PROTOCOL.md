# gclone engine protocol

Each engine runs in its own isolated process/environment and communicates with `gclone.exe` using one JSON object per line over stdin/stdout.

Normal frontend flow is intentionally lazy: selecting a bundled model does not start its worker. gclone already knows the capability set for the bundled engine version and renders the appropriate controls immediately. The worker is started only when Generate actually needs inference.

Commands supported by workers:

- `capabilities` — returns feature flags plus optional metadata such as pipe-delimited `language_options`. Retained for diagnostics/compatibility even though normal model selection no longer launches a worker just to request it.
- `generate` — begins reference-audio preparation and synthesis. The original sample is never modified.
- `unload` — releases the model where possible.
- `shutdown` — terminates the worker.

Current generation keys include `language`, `reference_transcript`, `x_vector_only`, `emotion_text`, `emotion_strength`, `duration_factor`, and advanced sampling parameters supported by the selected engine.

Workers may emit non-terminal `status` events while loading/preparing/generating. `capabilities`, `result`, and `error` are terminal responses for one request.

Stdout is protocol-only. Upstream logging and tracebacks belong on stderr.
