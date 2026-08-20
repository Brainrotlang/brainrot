// tests/run_wasm_tests.mjs
//
// Node.js smoke test for `make wasm-test`'s output (tests/brainrot-test.wasm
// / tests/brainrot-test.mjs) -- production stdrot/*.c PLUS tests/stdrot/*.c
// (test-only natives, see that directory's own file comment) statically
// linked in, the wasm equivalent of tests/libstdrot.so. Runs the same
// fixtures as tests/test_brainrot.py (test_cases/*.brainrot vs.
// tests/expected_results.json) through it instead of the native binary, and
// applies the same comparison rules, so the wasm target is held to the same
// bar as the native one rather than a separate, looser one -- with no
// fixtures skipped: the pointer-ABI/return-type-enforcement fixtures that
// depend on tests/stdrot/*.c exercise void*/uintptr_t/pointer_level/
// pointer-sized boxes whose representation genuinely differs between
// wasm32 (ILP32) and native (LP64), so this repo's own C source being
// identical between targets is exactly why they need to actually run here
// too, not a reason to skip them.
//
// Deliberately NOT `make wasm`'s plain brainrot.mjs (the artifact that gets
// uploaded/shipped) -- this harness's whole point is exercising things
// production code doesn't register, and the two must stay separate for the
// same reason tests/libstdrot.so and the native libstdrot.so do.
//
// Two fixtures (WASM_EXPECTED_OVERRIDES below) get a wasm-specific expected
// value instead of native's — see the comment next to it for why. They are
// still run and still asserted on, just against a different, equally exact
// string, so a regression in either one still fails this harness.
//
// Usage: node tests/run_wasm_tests.mjs   (run from the repo root, after `make wasm-test`)

import { readFileSync, existsSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const wasmJsPath = path.join(scriptDir, "brainrot-test.mjs");
const testCasesDir = path.join(repoRoot, "test_cases");

if (!existsSync(wasmJsPath)) {
  console.error(`brainrot-test.mjs not found at ${wasmJsPath} — run "make wasm-test" first.`);
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

// Mirrors the stdin fixtures test_brainrot.py feeds via `echo '...' | brainrot`
// (and, for the native_call_* entries, `printf '...' | brainrot`).
const STDIN_BY_PREFIX = [
  ["slorp_int", "42\n"],
  ["slorp_short", "69\n"],
  ["slorp_float", "3.14\n"],
  ["slorp_double", "3.141592\n"],
  ["slorp_char", "c\n"],
  ["slorp_bool", "1\n"],
  ["slorp_string", "skibidi bop bop yes yes\n"],
  ["native_call_self_init", "42\n"],
  ["native_call_loop", "1\n2\n3\n"],
  ["native_call_string_arg", "skibidi\nq\n"],
  ["native_call_do_while", "5\n50\n6\n150\n"],
];

function stdinFor(example) {
  const hit = STDIN_BY_PREFIX.find(([prefix]) => example.startsWith(prefix));
  return hit ? hit[1] : "";
}

// wasm32 uses the ILP32 data model (long = 4 bytes) vs native's LP64
// (long = 8 bytes), so sizeof(giga) genuinely differs — inherent to the
// wasm32 target, not a bug in Brainrot's sizeof logic (see ast.c's use of
// the real C sizeof(long)). These fixtures still run; they're just checked
// against the wasm-correct value instead of native's, so a real regression
// (wrong output, not just "still doesn't match native") still fails here.
// https://github.com/Brainrotlang/brainrot/issues/177
const WASM_EXPECTED_OVERRIDES = {
  giga: "4\n4",
  giga_array: "1\n2\n3\n12",
};

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

for (const [example, nativeExpectedOutput] of Object.entries(expectedResults)) {
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
  `\n${passed} passed, ${failures} failed (${overridden} against a wasm-specific expected value) (${Object.keys(expectedResults).length} total)`,
);
process.exit(failures > 0 ? 1 : 0);
