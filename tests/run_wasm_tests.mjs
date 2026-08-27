// tests/run_wasm_tests.mjs
//
// Node.js smoke test for the wasm build's output. Runs the same fixtures as
// tests/test_brainrot.py (test_cases/*.brainrot vs. tests/expected_results.json)
// through it instead of the native binary, and applies the same comparison
// rules, so the wasm target is held to the same bar as the native one rather
// than a separate, looser one.
//
// Two modes, selected by argv[2] -- deliberately NOT one module silently
// standing in for the other, after this harness once made exactly that
// mistake (loading only the test-augmented module meant the actual shipped
// brainrot.wasm/.mjs got compiled but never executed -- a green run here
// proved nothing about the artifact CI uploads):
//
//   test (default): tests/brainrot-test.mjs, built by `make wasm-test` --
//     production stdrot/*.c PLUS tests/stdrot/*.c (test-only natives, see
//     that directory's own file comment) statically linked in, the wasm
//     equivalent of tests/libstdrot.so. Runs every fixture, no skips: the
//     pointer-ABI/return-type-enforcement fixtures that depend on
//     tests/stdrot/*.c exercise void*/uintptr_t/pointer_level/pointer-sized
//     boxes whose representation genuinely differs between wasm32 (ILP32)
//     and native (LP64), so this repo's own C source being identical
//     between targets is exactly why they need to actually run here too.
//
//   production: brainrot.mjs, built by plain `make wasm` -- the artifact
//     that actually gets uploaded/shipped. Runs every fixture that doesn't
//     need a tests/stdrot/*.c native (WASM_PRODUCTION_SKIP below), since
//     those natives are never linked into this build by design. This is
//     the run that actually proves the shipped module starts up and works,
//     not just that a debug-only superset of it does -- the two builds
//     differ in more than just "extra natives" (linker section contents,
//     registry count, layout, startup registry iteration all change too),
//     so a green test-mode run does not substitute for this one.
//
// A handful of fixtures (WASM_EXPECTED_OVERRIDES below) get a wasm-specific
// expected value instead of native's — see the comment next to it for why.
// They are still run and still asserted on, just against a different,
// equally exact string, so a regression in either one still fails this
// harness. Override strings don't need a trailing newline to match a
// multi-line native expected string (e.g. "8\n4" vs. native's "16\n8\n")
// -- both sides of the comparison are .trim()'d below before comparing.
//
// Usage: node tests/run_wasm_tests.mjs [test|production]
//   (run from the repo root, after `make wasm-test` and/or `make wasm`)

import { readFileSync, existsSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const mode = process.argv[2] === "production" ? "production" : "test";
const wasmJsPath =
  mode === "production"
    ? path.join(repoRoot, "brainrot.mjs")
    : path.join(scriptDir, "brainrot-test.mjs");
const testCasesDir = path.join(repoRoot, "test_cases");

if (!existsSync(wasmJsPath)) {
  const buildCmd = mode === "production" ? "make wasm" : "make wasm-test";
  console.error(`${path.basename(wasmJsPath)} not found at ${wasmJsPath} — run "${buildCmd}" first.`);
  process.exit(1);
}

const createBrainrotModule = (await import(wasmJsPath)).default;
const expectedResults = JSON.parse(
  readFileSync(path.join(scriptDir, "expected_results.json"), "utf8"),
);

// #cooked (Brainrot's #include) resolves relative to the including file's
// own directory and echoes that file's basename in diagnostics (see
// resolve_cooked_path/basename_copy in lang.l). A handful of fixtures
// (cooked_*) rely on sibling files under test_cases/ being reachable that
// way, so the whole directory — not just the one file under test — needs to
// exist in each module's virtual FS, at the same relative layout, for those
// lookups and error messages to match native.
function listFilesRecursive(dir, base = dir) {
  const out = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      out.push(...listFilesRecursive(full, base));
    } else {
      out.push(path.relative(base, full));
    }
  }
  return out;
}

const testCaseFiles = listFilesRecursive(testCasesDir);

// Single source of truth for which fixtures read stdin and what to feed
// them -- an ordered list of [prefix, stdin] pairs, first startsWith() match
// wins. Loaded from the same tests/stdin_fixtures.json tests/test_brainrot.py
// (Python) reads, rather than each harness keeping its own copy: a fixture
// added to only one of them previously shipped broken (PR #230's wasm job
// failed on 9 fixtures whose stdin only existed in test_brainrot.py's old
// inline elif-chain, not in this file's separate hardcoded table).
const STDIN_BY_PREFIX = JSON.parse(
  readFileSync(path.join(scriptDir, "stdin_fixtures.json"), "utf8"),
);

function stdinFor(example) {
  const hit = STDIN_BY_PREFIX.find(([prefix]) => example.startsWith(prefix));
  return hit ? hit[1] : "";
}

// wasm32 uses the ILP32 data model (long = 4 bytes, pointers = 4 bytes) vs
// native's LP64 (long = 8 bytes, pointers = 8 bytes), so sizeof(giga) and
// sizeof(a pointer) genuinely differ — inherent to the wasm32 target, not
// a bug in Brainrot's sizeof logic (see ast.c's use of the real C
// sizeof(long)/sizeof(uintptr_t)). These fixtures still run; they're just
// checked against the wasm-correct value instead of native's, so a real
// regression (wrong output, not just "still doesn't match native") still
// fails here.
// https://github.com/Brainrotlang/brainrot/issues/177
const WASM_EXPECTED_OVERRIDES = {
  giga: "4\n4",
  giga_array: "1\n2\n3\n12",
  native_sizeof_ptr_result: "4",
  native_identity_abi_type_char_array: "8",
  native_void_pointer_struct_field: "8",
  // struct_field_long_modifier's `Meters` field is a `lit giga rizz`
  // (long) alias: 4 bytes/4-aligned on wasm32 ILP32 vs 8/8 on native
  // LP64, same root cause as `giga` above. `yap unit` (1 byte) then
  // pads to 4 (not native's 8), so the struct is 8 bytes total, and
  // the member's own maxxing(d.value) is 4 (not native's 8) too.
  struct_field_long_modifier: "8\n4",
  // struct_array_field_ptr's `rizz *ptrs[2];` is 2 pointers: 4 bytes
  // each on wasm32 ILP32 (8 total) vs 8 bytes each on native LP64 (16
  // total) -- same root cause as native_void_pointer_struct_field above.
  struct_array_field_ptr: "ptr0 correct\nptr1 correct\n8",
  // lang.l's "cannot find module" diagnostic is deliberately worded
  // differently under STDROT_STATIC (wasm, no dynamic loader at all) vs.
  // native: native mentions both "<name>.brainrot" and "<name>.so" as
  // candidates the search path checked, wasm mentions only
  // "<name>.brainrot" since module_path_resolve() (module_path.c) never
  // even looks for a ".so" there. See lang.l's handle_cooked_module_
  // directive for the #ifdef STDROT_STATIC split this mirrors.
  cooked_module_search_path:
    "Error: cooked_module_search_path.brainrot:10: cannot find module " +
    "'mathmod' (looked for 'mathmod.brainrot' via $BRAINROT_PATH, then " +
    "either the install module directory or a 'stdrot' directory next " +
    "to this executable, whichever applies)",
  // gamba is cryptographically safe RNG backed by OpenSSL RAND_bytes
  // (issue #215). The wasm build is deliberately OpenSSL-free (issue #175),
  // so gamba is a documented erroring stub there (stdrot/gamba.c's
  // STDROT_STATIC branch) rather than a weaker generator. The happy-path
  // fixture draws randomness on its first call (`rizz fixed = gamba(1, 1);`,
  // line 7) and so aborts here instead of printing native's property
  // assertions. The two invalid-range fixtures (gamba_range_fail,
  // gamba_zero_fail) reject BEFORE touching the CSPRNG, so they emit the
  // same error on wasm as native and need no override.
  gamba: "Error: gamba: CSPRNG unavailable in this build (no OpenSSL) at line 7",
  // Zero-argument gamba() aborts on the wasm stub too. Its error line comes
  // from the call node (line 8), NOT a first-argument node it doesn't have --
  // guarding that the no-arg form reports the real line, not "line 0".
  gamba_noarg:
    "Error: gamba: CSPRNG unavailable in this build (no OpenSSL) at line 8",
};

// Fixtures that call a tests/stdrot/*.c native (poke_int, peek_int,
// test_ptr_source, lying_double, lying_bool, lying_ptr_return,
// legacy_ptr_leak, legacy_int, legacy_int_prints, legacy_string,
// legacy_cstring, legacy_void, legacy_returns_any_tag, cstring_return,
// takes_cstring, takes_char, identity, legacy_scratch_string,
// legacy_mutate_scratch_and_return_int) --
// only meaningful in "test" mode against tests/brainrot-test.mjs, which is
// the only build those natives are ever linked into. In "production" mode
// they'd all fail with "Undefined function" against brainrot.mjs, which is
// expected (that module correctly doesn't have them) rather than a
// regression worth asserting against.
const WASM_PRODUCTION_SKIP = new Set([
  "native_ptr_param_return",
  "semantic_error_native_ptr_wrong_depth",
  "semantic_error_native_ptr_return_scalar_init",
  "semantic_error_native_ptr_result_scalar_param",
  "native_return_type_numeric_coercion",
  "native_return_type_abi_violation_incompatible",
  "native_return_type_abi_violation_ptr_mismatch",
  "native_return_type_abi_violation_any_pointer_leak",
  "native_return_type_abi_violation_any_tag",
  "semantic_error_native_cstring_return",
  "native_return_type_any_numeric_coercion",
  "native_return_type_any_incompatible_context",
  "native_return_type_any_bool_context",
  "native_return_type_any_cstring_leak",
  "native_return_type_any_void_value_context",
  "native_return_type_any_void_statement",
  "native_typed_param_from_legacy_any",
  "semantic_error_native_ptr_dest_from_legacy_any",
  "native_cstring_param_char_array",
  "native_char_array_access",
  "native_char_struct_access",
  "native_nested_struct_access",
  "native_char_dereference",
  "native_identity_enum_variable",
  "native_legacy_variadic_not_promoted",
  "native_legacy_void_variadic_tail_fail",
  "identity_cstring_as_string_use_after_free",
  "semantic_error_native_sizeof_legacy_unknown",
  "native_call_cache_growth",
  "native_char_param_scalar",
  "identity_string_use_after_free",
  "identity_ownership_nonstring_result",
  "semantic_error_native_sizeof_legacy_nested",
  "native_identity_abi_type_char_literal",
  "native_identity_abi_type_char_array",
  "native_sizeof_ptr_result",
  "native_zero_arg_string_ownership",
  "native_void_pointer_from_native",
  "native_struct_ptr_field_arg",
  "native_void_pointer_parameter",
  "native_user_pointer_return",
  "native_pointer_array_element_arg",
  "semantic_error_slorp_ptr_native_arg",
  "semantic_error_void_pointer_dereference_write",
  "semantic_error_void_pointer_dereference_read",
  "native_void_double_pointer_dereference",
  "native_void_pointer_array_element",
  "semantic_error_opaque_pointer_dereference",
  "semantic_error_opaque_pointer_arithmetic",
  "native_void_double_pointer_arithmetic",
  "native_return_reentrant_native_call",
  "native_void_pointer_array_braced_init",
]);

// Runs one program in a fresh module instance — the interpreter has global
// state (current_scope, arena allocations, stdrot's symbol table) that is
// not designed to be re-entered, so each run gets its own instance exactly
// like each native run gets its own process.
async function runOne(example, stdin) {
  const stdoutChunks = [];
  const stderrChunks = [];
  let stdinPos = 0;

  const mod = await createBrainrotModule({
    print: (text) => stdoutChunks.push(text),
    printErr: (text) => stderrChunks.push(text),
    stdin: () => (stdinPos < stdin.length ? stdin.charCodeAt(stdinPos++) : null),
    noInitialRun: true,
  });

  const vfsRoot = "/test_cases";
  mod.FS.mkdirTree(vfsRoot);
  for (const rel of testCaseFiles) {
    const vfsRel = rel.split(path.sep).join("/");
    const vfsPath = `${vfsRoot}/${vfsRel}`;
    const vfsDir = path.posix.dirname(vfsPath);
    if (vfsDir !== vfsRoot) mod.FS.mkdirTree(vfsDir);
    mod.FS.writeFile(vfsPath, readFileSync(path.join(testCasesDir, rel)));
  }

  const vfsSourcePath = `${vfsRoot}/${example}.brainrot`;
  let exitCode = 0;
  try {
    exitCode = mod.callMain([vfsSourcePath]);
  } catch (e) {
    // Emscripten throws ExitStatus for a clean exit(); anything else is a
    // real crash and should fail the test loudly rather than being folded
    // into "stderr output".
    if (e && typeof e.status === "number") {
      exitCode = e.status;
    } else {
      throw e;
    }
  }

  return {
    stdout: stdoutChunks.join("\n") + (stdoutChunks.length ? "\n" : ""),
    stderr: stderrChunks.join("\n") + (stderrChunks.length ? "\n" : ""),
    exitCode,
  };
}

let failures = 0;
let passed = 0;
let overridden = 0;
let skipped = 0;

for (const [example, nativeExpectedOutput] of Object.entries(expectedResults)) {
  if (mode === "production" && WASM_PRODUCTION_SKIP.has(example)) {
    skipped++;
    continue;
  }

  const expectedOutput = WASM_EXPECTED_OVERRIDES[example] ?? nativeExpectedOutput;
  if (example in WASM_EXPECTED_OVERRIDES) overridden++;

  const stdin = stdinFor(example);

  let result;
  try {
    result = await runOne(example, stdin);
  } catch (e) {
    failures++;
    console.error(`✗ ${example}: threw ${e}`);
    continue;
  }

  // "ExitCode:N" mirrors test_brainrot.py's convention: asserts only the
  // exit code, for fixtures whose only observable behavior *is* the exit
  // code (e.g. ragequit/chill have no return value to print).
  if (expectedOutput.startsWith("ExitCode:")) {
    const expectedCode = Number(expectedOutput.split(":", 2)[1]);
    if (result.exitCode !== expectedCode) {
      failures++;
      console.error(
        `✗ ${example}: expected exit ${expectedCode}, got ${result.exitCode}`,
      );
      continue;
    }
    passed++;
    continue;
  }

  const stdoutTrimmed = result.stdout.trim();
  const stderrTrimmed = result.stderr.trim();
  let actualOutput = stdoutTrimmed || stderrTrimmed;

  if (expectedOutput.includes("Stderr:") && stdoutTrimmed) {
    actualOutput = `${stdoutTrimmed}\nStderr:\n${stderrTrimmed}`;
  }

  const expectedTrimmed = expectedOutput.trim();
  if (actualOutput !== expectedTrimmed) {
    failures++;
    console.error(
      `✗ ${example}\n  expected: ${JSON.stringify(expectedTrimmed)}\n  actual:   ${JSON.stringify(actualOutput)}`,
    );
    continue;
  }

  if (!expectedOutput.includes("Error:") && result.exitCode !== 0) {
    failures++;
    console.error(`✗ ${example}: expected exit 0, got ${result.exitCode}`);
    continue;
  }

  passed++;
}

console.log(
  `\n[${mode}] ${passed} passed, ${failures} failed, ${skipped} skipped (${overridden} against a wasm-specific expected value) (${Object.keys(expectedResults).length} total)`,
);
process.exit(failures > 0 ? 1 : 0);
