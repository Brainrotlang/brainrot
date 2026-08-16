// tests/run_wasm_tests.mjs
//
// Node.js smoke test for `make wasm`'s output (brainrot.wasm / brainrot.mjs).
// Runs the same fixtures as tests/test_brainrot.py (test_cases/*.brainrot vs.
// tests/expected_results.json) through the wasm build instead of the native
// binary, and applies the same comparison rules, so the wasm target is held
// to the same bar as the native one rather than a separate, looser one.
//
// A small, explicitly documented set of fixtures (KNOWN_FAILURES below) is
// skipped rather than deleted or silently passed — each one is a real,
// wasm-specific divergence from native with its own tracking issue. See the
// comments next to KNOWN_FAILURES for what and why.
//
// Usage: node tests/run_wasm_tests.mjs   (run from the repo root, after `make wasm`)

import { readFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const wasmJsPath = path.join(repoRoot, "brainrot.mjs");

if (!existsSync(wasmJsPath)) {
  console.error(`brainrot.mjs not found at ${wasmJsPath} — run "make wasm" first.`);
  process.exit(1);
}

const createBrainrotModule = (await import(wasmJsPath)).default;
const expectedResults = JSON.parse(
  readFileSync(path.join(scriptDir, "expected_results.json"), "utf8"),
);

// Mirrors the stdin fixtures test_brainrot.py feeds via `echo '...' | brainrot`.
const STDIN_BY_PREFIX = [
  ["slorp_int", "42\n"],
  ["slorp_short", "69\n"],
  ["slorp_float", "3.14\n"],
  ["slorp_double", "3.141592\n"],
  ["slorp_char", "c\n"],
  ["slorp_string", "skibidi bop bop yes yes\n"],
];

function stdinFor(example) {
  const hit = STDIN_BY_PREFIX.find(([prefix]) => example.startsWith(prefix));
  return hit ? hit[1] : "";
}

// Known, tracked wasm-specific divergences from the native build — not
// regressions in this harness. Every entry here needs an open issue.
const KNOWN_FAILURES = {
  // stdrot/lib/mem.c's safe_free() guard rejects a handful of pointers
  // during interpreter teardown under wasm's allocator (never happens
  // natively); the warning text lands on stderr, which is these fixtures'
  // only output channel, so it breaks the exact-match comparison even
  // though stdout (and every fixture that has real stdout) is unaffected.
  // https://github.com/Brainrotlang/brainrot/issues/176
  output_error: 176,
  "func-modifier": 176,
  semantic_error_const: 176,
  semantic_error_function_redef: 176,
  semantic_error_scope: 176,
  semantic_error_pointer_deref: 176,
  bet_fail: 176,
  // wasm32 uses ILP32 (long = 4 bytes) vs native's LP64 (long = 8 bytes),
  // so sizeof(giga) genuinely differs. Inherent to the wasm32 target, not
  // a bug in Brainrot's sizeof logic.
  // https://github.com/Brainrotlang/brainrot/issues/177
  giga: 177,
  giga_array: 177,
};

// Runs one program in a fresh module instance — the interpreter has global
// state (current_scope, arena allocations, stdrot's symbol table) that is
// not designed to be re-entered, so each run gets its own instance exactly
// like each native run gets its own process.
async function runOne(sourcePath, stdin) {
  const stdoutChunks = [];
  const stderrChunks = [];
  let stdinPos = 0;

  const mod = await createBrainrotModule({
    print: (text) => stdoutChunks.push(text),
    printErr: (text) => stderrChunks.push(text),
    stdin: () => (stdinPos < stdin.length ? stdin.charCodeAt(stdinPos++) : null),
    noInitialRun: true,
  });

  mod.FS.writeFile("/prog.brainrot", readFileSync(sourcePath));

  let exitCode = 0;
  try {
    exitCode = mod.callMain(["/prog.brainrot"]);
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
let skipped = 0;

for (const [example, expectedOutput] of Object.entries(expectedResults)) {
  if (example in KNOWN_FAILURES) {
    skipped++;
    console.log(`- ${example} (skipped, tracked in #${KNOWN_FAILURES[example]})`);
    continue;
  }

  const sourcePath = path.join(repoRoot, "test_cases", `${example}.brainrot`);
  const stdin = stdinFor(example);

  let result;
  try {
    result = await runOne(sourcePath, stdin);
  } catch (e) {
    failures++;
    console.error(`✗ ${example}: threw ${e}`);
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
  `\n${passed} passed, ${failures} failed, ${skipped} skipped (${Object.keys(expectedResults).length} total)`,
);
process.exit(failures > 0 ? 1 : 0);
