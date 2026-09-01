// tests/run_wasm_examples_check.mjs
//
// Runs every examples/*.brainrot program through both the native `brainrot`
// binary and the wasm build, and asserts their stdout AND stderr match.
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
import { spawnSync } from "node:child_process";
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

// examples/sieve_of_eras.brainrot trips a pre-existing UBSan misaligned-load
// diagnostic on native's stderr (see #180) — that's the ASan/UBSan runtime
// talking, not the interpreter, and the wasm build carries no such
// instrumentation (no -fsanitize in WASM_CFLAGS), so it never prints that
// noise. Comparing native's stderr against wasm's for this one file would
// fail on a difference that isn't actually a wasm bug. No example uses
// `baka()` (the only builtin that writes real program output to stderr),
// so wasm's stderr is still asserted empty here — same bar as every other
// example gets against native, just not diffed against native's directly.
const KNOWN_NATIVE_ONLY_STDERR = new Set(["sieve_of_eras.brainrot"]);

// modules_named.brainrot demonstrates #cooked <name> resolved via
// $BRAINROT_PATH (see its own file comment: it's meant to be run as
// `BRAINROT_PATH=examples ./brainrot examples/modules_named.brainrot`) --
// neither runNative() nor runWasm() below sets that env var, so both
// deterministically fail to find the "mathutils" module. That's expected
// here (this checker isn't set up to exercise the BRAINROT_PATH-set case),
// but lang.l's "cannot find module" diagnostic is deliberately worded
// differently under STDROT_STATIC (wasm) vs. native -- native additionally
// mentions ".so" as a candidate, wasm doesn't, since module_path_resolve()
// (module_path.c) never looks for a native module in a build with no
// dynamic loader at all. An exact stderr match would fail on that wording
// difference alone even though both sides are reporting the identical
// underlying failure, so this file is checked for stderr *content*
// (both name the same missing module) instead of exact equality.
const KNOWN_STDERR_WORDING_DIVERGENCE = new Set(["modules_named.brainrot"]);

// #cooked (Brainrot's #include) resolves relative to the including file's
// own directory (see resolve_cooked_path in lang.l) — modules.brainrot
// #cookeds mathutils.brainrot as a sibling, so the whole examples/ directory,
// not just the one file under test, needs to exist in the virtual FS at the
// same relative layout for that lookup to succeed.
async function runWasm(file) {
  const stdoutChunks = [];
  const stderrChunks = [];
  const mod = await createBrainrotModule({
    print: (text) => stdoutChunks.push(text),
    printErr: (text) => stderrChunks.push(text),
    noInitialRun: true,
  });
  const vfsRoot = "/examples";
  mod.FS.mkdirTree(vfsRoot);
  for (const name of exampleFiles) {
    mod.FS.writeFile(`${vfsRoot}/${name}`, readFileSync(path.join(examplesDir, name)));
  }
  try {
    mod.callMain([`${vfsRoot}/${file}`]);
  } catch (e) {
    if (!(e && typeof e.status === "number")) throw e;
  }
  return {
    stdout: stdoutChunks.join("\n") + (stdoutChunks.length ? "\n" : ""),
    stderr: stderrChunks.join("\n") + (stderrChunks.length ? "\n" : ""),
  };
}

function runNative(sourcePath) {
  const result = spawnSync(nativeBinary, [sourcePath], { encoding: "utf8" });
  return { stdout: result.stdout, stderr: result.stderr };
}

let failures = 0;

for (const file of exampleFiles) {
  const sourcePath = path.join(examplesDir, file);
  const native = runNative(sourcePath);
  const wasm = await runWasm(file);

  const nativeOut = native.stdout.trim();
  const wasmOut = wasm.stdout.trim();
  const wasmErr = wasm.stderr.trim();

  if (nativeOut !== wasmOut) {
    failures++;
    console.error(
      `✗ examples/${file} (stdout)\n  native: ${JSON.stringify(nativeOut)}\n  wasm:   ${JSON.stringify(wasmOut)}`,
    );
    continue;
  }

  if (KNOWN_NATIVE_ONLY_STDERR.has(file)) {
    if (wasmErr !== "") {
      failures++;
      console.error(`✗ examples/${file} (stderr): expected empty, got ${JSON.stringify(wasmErr)}`);
      continue;
    }
  } else if (KNOWN_STDERR_WORDING_DIVERGENCE.has(file)) {
    const nativeErr = native.stderr.trim();
    const commonPrefix = "Error: modules_named.brainrot:5: cannot find module 'mathutils'";
    if (!nativeErr.startsWith(commonPrefix) || !wasmErr.startsWith(commonPrefix)) {
      failures++;
      console.error(
        `✗ examples/${file} (stderr): expected both to start with ${JSON.stringify(commonPrefix)}\n  native: ${JSON.stringify(nativeErr)}\n  wasm:   ${JSON.stringify(wasmErr)}`,
      );
      continue;
    }
  } else {
    const nativeErr = native.stderr.trim();
    if (nativeErr !== wasmErr) {
      failures++;
      console.error(
        `✗ examples/${file} (stderr)\n  native: ${JSON.stringify(nativeErr)}\n  wasm:   ${JSON.stringify(wasmErr)}`,
      );
      continue;
    }
  }

  console.log(`✓ examples/${file}`);
}

console.log(`\n${exampleFiles.length - failures}/${exampleFiles.length} examples match native (stdout + stderr)`);
process.exit(failures > 0 ? 1 : 0);
