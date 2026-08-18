# gclone engine protocol

Each engine runs in its own process/environment and communicates with `gclone.exe` using one JSON object per line over stdin/stdout.

Commands currently used by the frontend:

- `capabilities` — returns booleans that drive which UI controls exist for the selected backend.
- `generate` — begins reference-audio preparation and synthesis. The original sample is never modified.
- `unload` — releases the model where possible.
- `shutdown` — terminates the worker.

Workers may emit non-terminal `status` events while loading/preparing/generating. `capabilities`, `result`, and `error` are terminal responses for one request.

Stdout is protocol-only. Upstream logging and tracebacks belong on stderr.
