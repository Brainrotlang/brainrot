// tests/run_wasm_tests.mjs
//
// Node.js smoke test for `make wasm`'s output (brainrot.wasm / brainrot.mjs).
// Runs the same fixtures as tests/test_brainrot.py (test_cases/*.brainrot vs.
// tests/expected_results.json) through the wasm build instead of the native
// binary, and applies the same comparison rules, so the wasm target is held
// to the same bar as the native one rather than a separate, looser one.
//
// Two fixtures (WASM_EXPECTED_OVERRIDES below) get a wasm-specific expected
// value instead of native's — see the comment next to it for why. They are
// still run and still asserted on, just against a different, equally exact
// string, so a regression in either one still fails this harness.
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
let overridden = 0;

for (const [example, nativeExpectedOutput] of Object.entries(expectedResults)) {
  const expectedOutput = WASM_EXPECTED_OVERRIDES[example] ?? nativeExpectedOutput;
  if (example in WASM_EXPECTED_OVERRIDES) overridden++;

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
  `\n${passed} passed, ${failures} failed (${overridden} against a wasm-specific expected value) (${Object.keys(expectedResults).length} total)`,
);
process.exit(failures > 0 ? 1 : 0);
