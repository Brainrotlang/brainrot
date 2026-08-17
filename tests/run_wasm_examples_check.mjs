// tests/run_wasm_examples_check.mjs
//
// Runs every examples/*.brainrot program through both the native `brainrot`
// binary and the wasm build, and asserts their stdout is byte-identical.
// tests/run_wasm_tests.mjs covers test_cases/*.brainrot (the pytest fixture
// set), but examples/ — what the README and this repo's docs actually point
// users at — isn't a strict subset of that (only hello_world and fizz_buzz
// overlap by name, and fizz_buzz's content differs), so it needs its own
// direct check rather than being assumed covered.
//
// Diffs against a real native run instead of a hardcoded expected string, so
// this can't go stale the way a copy-pasted expected value could.
//
// Usage: node tests/run_wasm_examples_check.mjs
//   (run from the repo root, after both `make` and `make wasm`)

import { readFileSync, existsSync, readdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "..");
const nativeBinary = path.join(repoRoot, "brainrot");
const wasmJsPath = path.join(repoRoot, "brainrot.mjs");

for (const [label, p] of [
  ["brainrot (native)", nativeBinary],
  ["brainrot.mjs (wasm)", wasmJsPath],
]) {
  if (!existsSync(p)) {
    console.error(`${label} not found at ${p} — build it first (make / make wasm).`);
    process.exit(1);
  }
}

const createBrainrotModule = (await import(wasmJsPath)).default;
const examplesDir = path.join(repoRoot, "examples");
const exampleFiles = readdirSync(examplesDir).filter((f) => f.endsWith(".brainrot"));

async function runWasm(sourcePath) {
  const stdoutChunks = [];
  const mod = await createBrainrotModule({
    print: (text) => stdoutChunks.push(text),
    printErr: () => {},
    noInitialRun: true,
  });
  mod.FS.writeFile("/prog.brainrot", readFileSync(sourcePath));
  try {
    mod.callMain(["/prog.brainrot"]);
  } catch (e) {
    if (!(e && typeof e.status === "number")) throw e;
  }
  return stdoutChunks.join("\n") + (stdoutChunks.length ? "\n" : "");
}

function runNative(sourcePath) {
  return execFileSync(nativeBinary, [sourcePath], { encoding: "utf8" });
}

let failures = 0;

for (const file of exampleFiles) {
  const sourcePath = path.join(examplesDir, file);
  const nativeOut = runNative(sourcePath).trim();
  const wasmOut = (await runWasm(sourcePath)).trim();

  if (nativeOut !== wasmOut) {
    failures++;
    console.error(
      `✗ examples/${file}\n  native: ${JSON.stringify(nativeOut)}\n  wasm:   ${JSON.stringify(wasmOut)}`,
    );
    continue;
  }
  console.log(`✓ examples/${file}`);
}

console.log(`\n${exampleFiles.length - failures}/${exampleFiles.length} examples match native stdout`);
process.exit(failures > 0 ? 1 : 0);
