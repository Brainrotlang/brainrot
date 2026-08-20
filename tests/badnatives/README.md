# tests/badnatives/

Each `.c` file here registers exactly one deliberately malformed
`StdrotEntry` (via `STDROT_EXPORT_SIG`/`STDROT_EXPORT_SIG_IDENTITY`, or a raw
struct literal for shapes the macros can't express) and nothing else --
built as its own tiny `.so` (`make badnatives`) linked against only
`stdrot/registry.c`, no production natives. `validate_native_registry()`
(stdrot.c) must reject each one at `stdrot_load()` time, before any
`.brainrot` program gets a chance to run: `tests/test_brainrot.py`'s
`test_bad_registry_*` functions point `STDROT_LIB_PATH` at one of these
`.so`s for a single subprocess invocation (never the shared
`tests/libstdrot.so` the rest of the suite uses) and assert the process
exits 1 with the expected `stdrot: ...` message on stderr.

These are load-time registry defects, not language-level bugs a
`test_cases/*.brainrot` fixture could express -- a malformed native binding
is a bug in the (hypothetical, hand-written) C binding itself, never
something a Brainrot program can trigger.
