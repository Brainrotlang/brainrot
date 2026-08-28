import subprocess
import json
import os
import shutil
import pytest

# Get the absolute path to the directory containing the script
script_dir = os.path.dirname(__file__)

# Construct the full path to the JSON file with expected results
file_path = os.path.join(script_dir, "expected_results.json")

# Load expected results from the JSON file
with open(file_path, "r") as file:
    expected_results = json.load(file)

# Single source of truth for which fixtures read stdin and what to feed
# them -- an ordered list of [prefix, stdin] pairs, first startswith() match
# wins. Shared verbatim with tests/run_wasm_tests.mjs (Node): both harnesses
# load this same file rather than keeping their own copy, since a fixture
# added to only one of them previously shipped broken (PR #230's wasm job
# failed on 9 fixtures whose stdin only existed in this file's old inline
# elif-chain, not in the wasm runner's separate hardcoded table).
with open(os.path.join(script_dir, "stdin_fixtures.json"), "r") as file:
    stdin_by_prefix = json.load(file)

def stdin_for(example):
    for prefix, stdin in stdin_by_prefix:
        if example.startswith(prefix):
            return stdin
    return None

@pytest.mark.parametrize("example,expected_output", expected_results.items())
def test_brainrot_examples(example, expected_output):
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    example_file_path = os.path.abspath(os.path.join(script_dir, f"../test_cases/{example}.brainrot"))

    result = subprocess.run(
        [brainrot_path, example_file_path],
        input=stdin_for(example),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    # "ExitCode:N" asserts only the process's exit code -- for cases whose
    # observable behavior *is* the exit code (e.g. ragequit(1.5) truncating
    # to 1) and has no other way to surface a mismatch through stdout/stderr.
    if expected_output.startswith("ExitCode:"):
        expected_code = int(expected_output.split(":", 1)[1])
        assert result.returncode == expected_code, (
            f"Command for {example} exited {result.returncode}, expected {expected_code}\n"
            f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
        )
        return

    actual_output = result.stdout.strip() if result.stdout.strip() else result.stderr.strip()

    if "Stderr:" in expected_output and result.stdout.strip():
        actual_output = f"{result.stdout.strip()}\nStderr:\n{result.stderr.strip()}"

    assert actual_output == expected_output.strip(), (
        f"Output for {example} did not match.\n"
        f"Expected:\n{expected_output}\n"
        f"Actual:\n{actual_output}"
    )

    if "Error:" not in expected_output:
        assert result.returncode == 0, (
            f"Command for {example} failed with return code {result.returncode}\n"
            f"Stderr:\n{result.stderr}"
        )


# Regression test for the canonical `yap[N]` buffer form (#229/#230):
# `yap name[32]; slorp(name);` must not print the deprecated scalar
# write-back warning (execute_func_call(), stdrot.c) -- that warning is
# for the pre-#204 scalar witness convention (`rizz x; slorp(x);`), not
# for a char-array buffer slorp() already wrote into in place. The
# JSON-driven test above only inspects stderr when stdout is empty, so a
# stray warning alongside real stdout would otherwise go unnoticed.
def test_slorp_buffer_form_has_no_deprecation_warning():
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    example_file_path = os.path.abspath(
        os.path.join(script_dir, "../test_cases/slorp_buffer_no_deprecation_warning.brainrot"))

    result = subprocess.run(
        [brainrot_path, example_file_path],
        input="Chad\n",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    assert result.stdout.strip() == "hello Chad", (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert "deprecated" not in result.stderr, (
        f"yap[N] buffer form should not warn as deprecated\n"
        f"Stderr:\n{result.stderr}"
    )


# ── validate_native_registry() rejection tests ──────────────────────────────
# Each tests/badnatives/*.c file (built by `make badnatives`) registers
# exactly one deliberately malformed StdrotEntry; validate_native_registry()
# (stdrot.c) must reject it during stdrot_load(), before test_cases/
# trivial_no_op.brainrot's single statement ever runs. Unlike every other
# test in this file, these override STDROT_LIB_PATH per-subprocess (never
# the shared tests/libstdrot.so the rest of the suite uses) since the
# malformed registry must not be visible to any other test.
REGISTRY_REJECTION_CASES = [
    ("identity_non_any.so",
     "return_like_arg (0) names a parameter that isn't STDROT_ANY"),
    ("null_fn.so", "fn is NULL"),
    ("duplicate_name.so", "duplicate native export 'bad_duplicate'"),
    ("negative_pointer_level.so",
     "params[0].pointer_level (-1) must be >= 0"),
    ("pointer_level_without_ptr.so",
     "params[0].pointer_level (1) must be 0 when params[0].type isn't STDROT_PTR"),
    ("none_typed_param.so",
     "params[0].type must not be STDROT_NONE"),
    ("invalid_return_type.so",
     "return_type.type (999) is not a valid StdrotType"),
    ("invalid_param_type.so",
     "params[0].type (999) is not a valid StdrotType"),
    ("bad_api_table_negative_count.so",
     "registry function_count (-1) is not a plausible value"),
    ("bad_api_table_null_functions.so",
     "registry function_count (1) is > 0 but functions is NULL"),
]


@pytest.mark.parametrize("bad_lib,expected_message", REGISTRY_REJECTION_CASES)
def test_bad_registry_rejected_at_load(bad_lib, expected_message):
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    trivial_path = os.path.abspath(
        os.path.join(script_dir, "../test_cases/trivial_no_op.brainrot"))
    bad_lib_path = os.path.abspath(
        os.path.join(script_dir, "badnatives", bad_lib))

    assert os.path.exists(bad_lib_path), (
        f"{bad_lib_path} not found -- run `make badnatives` first")

    env = dict(os.environ, STDROT_LIB_PATH=bad_lib_path)
    result = subprocess.run([brainrot_path, trivial_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)

    assert result.returncode == 1, (
        f"Loading {bad_lib} should abort with exit code 1, got "
        f"{result.returncode}\nStdout:\n{result.stdout}\n"
        f"Stderr:\n{result.stderr}"
    )
    assert expected_message in result.stderr, (
        f"Expected stderr for {bad_lib} to contain {expected_message!r}\n"
        f"Actual stderr:\n{result.stderr}"
    )


# Round-16 review finding #1: tests/old_abi_sim/fake_pre_v2_registry.so
# (built by `make old-abi-sim`) simulates a libstdrot.so built before
# STDROT_ABI_VERSION/stdrot_get_api_v2() existed -- it exports only the
# OLD "stdrot_get_api" symbol, under the OLD {name, fn} layout. Proves
# stdrot_load() (stdrot.c) detects the missing versioned symbol and fails
# loudly instead of reinterpreting this .so's actual memory as the
# current ABI shape.
def test_old_abi_rejected_at_load():
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    trivial_path = os.path.abspath(
        os.path.join(script_dir, "../test_cases/trivial_no_op.brainrot"))
    old_abi_lib_path = os.path.abspath(
        os.path.join(script_dir, "old_abi_sim", "fake_pre_v2_registry.so"))

    assert os.path.exists(old_abi_lib_path), (
        f"{old_abi_lib_path} not found -- run `make old-abi-sim` first")

    env = dict(os.environ, STDROT_LIB_PATH=old_abi_lib_path)
    result = subprocess.run([brainrot_path, trivial_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)

    assert result.returncode == 1, (
        f"Loading a pre-ABI-versioning .so should abort with exit code "
        f"1, got {result.returncode}\nStdout:\n{result.stdout}\n"
        f"Stderr:\n{result.stderr}"
    )
    assert "stdrot_get_api_v2" in result.stderr, (
        f"Expected stderr to name the missing versioned entrypoint\n"
        f"Actual stderr:\n{result.stderr}"
    )


# ── #cooked <name> module search path (lib/module_path.c) ──────────────────
# test_cases/cooked_module_search_path.brainrot's own default run (no
# $BRAINROT_PATH set, via the generic loop above) already covers the
# "module not found" error path. Everything below needs a directory layout
# or a $PATH-style invocation the generic loop can't express, so -- like the
# badnatives/old_abi_sim tests above -- these run outside
# expected_results.json with their own env/layout.
#
# Search order (module_path.h): $BRAINROT_PATH, then EXACTLY ONE of
# {install module dir, in-tree "stdrot/" next to the running executable} --
# never both, decided by whether the running executable's own directory is
# the install bin directory. "The running executable" is resolved via
# /proc/self/exe (or _NSGetExecutablePath on macOS), never argv[0]/cwd, so
# these tests specifically exercise a bare $PATH-style invocation (argv[0]
# with no directory component) and a decoy in cwd -- the exact case
# argv[0]-based resolution would get wrong.

MATHMOD_SOURCE = os.path.join(script_dir, "modules", "mathmod.brainrot")
COOKED_MODULE_SEARCH_PATH_SOURCE = os.path.join(
    script_dir, "..", "test_cases", "cooked_module_search_path.brainrot")


def _copy_binary(dest_dir):
    """Copies the built `brainrot` binary into dest_dir, returns its path."""
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    copied_binary = dest_dir / "brainrot"
    shutil.copy(os.path.join(repo_root, "brainrot"), copied_binary)
    os.chmod(copied_binary, 0o755)
    return copied_binary


def test_module_search_path_hit():
    """$BRAINROT_PATH pointed at tests/modules/ resolves #cooked <mathmod>."""
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    source_path = os.path.abspath(COOKED_MODULE_SEARCH_PATH_SOURCE)
    modules_dir = os.path.abspath(os.path.join(script_dir, "modules"))

    env = dict(os.environ, BRAINROT_PATH=modules_dir)
    result = subprocess.run([brainrot_path, source_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)

    assert result.returncode == 0, (
        f"Expected success with BRAINROT_PATH={modules_dir}, got "
        f"{result.returncode}\nStdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "42\n", f"Actual stdout:\n{result.stdout}"


def test_module_in_tree_fallback(tmp_path):
    """With no $BRAINROT_PATH, "stdrot/" next to the executable still resolves.

    Copies the built binary into an empty tmp directory alongside a
    stdrot/mathmod.brainrot of its own, then runs that copy with
    $BRAINROT_PATH unset and while NOT the install bin directory (the
    default -- BRAINROT_TEST_INSTALL_BIN_DIR is left unset here) --
    module_path.c's in-tree tier is "stdrot/" next to the actual running
    executable, resolved independently of argv[0]/cwd. Runs with cwd=repo
    root so stdrot_load()'s own cwd-relative "./libstdrot.so" lookup
    (stdrot.c) still finds the real library; STDROT_LIB_PATH (set by `make
    test`, see the Makefile) takes priority over that lookup anyway and is
    inherited from os.environ regardless of cwd.
    """
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    source_path = os.path.join(
        repo_root, "test_cases", "cooked_module_search_path.brainrot")

    copied_binary = _copy_binary(tmp_path)
    (tmp_path / "stdrot").mkdir()
    shutil.copy(MATHMOD_SOURCE, tmp_path / "stdrot" / "mathmod.brainrot")

    env = dict(os.environ)
    env.pop("BRAINROT_PATH", None)
    env.pop("BRAINROT_TEST_INSTALL_BIN_DIR", None)
    result = subprocess.run([str(copied_binary), source_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, cwd=repo_root)

    assert result.returncode == 0, (
        f"Expected the in-tree fallback to resolve 'mathmod', got "
        f"{result.returncode}\nStdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "42\n", f"Actual stdout:\n{result.stdout}"


def test_module_search_path_precedence(tmp_path):
    """$BRAINROT_PATH outranks the in-tree "stdrot/" tier (module_path.c order).

    Puts a DIFFERENT "mathmod" module in each tier (tests/modules_shadow/ vs
    a copy of the binary's own stdrot/) and checks the $BRAINROT_PATH one
    wins -- Appendix B Q11 (docs/ROADMAP.md): the two tiers must not
    silently shadow each other in the wrong order.
    """
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    source_path = os.path.join(
        repo_root, "test_cases", "cooked_module_search_path.brainrot")
    shadow_dir = os.path.abspath(os.path.join(script_dir, "modules_shadow"))

    copied_binary = _copy_binary(tmp_path)
    (tmp_path / "stdrot").mkdir()
    shutil.copy(MATHMOD_SOURCE, tmp_path / "stdrot" / "mathmod.brainrot")

    env = dict(os.environ, BRAINROT_PATH=shadow_dir)
    env.pop("BRAINROT_TEST_INSTALL_BIN_DIR", None)
    result = subprocess.run([str(copied_binary), source_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, cwd=repo_root)

    assert result.returncode == 0, (
        f"Expected success, got {result.returncode}\n"
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "63\n", (
        f"Expected the $BRAINROT_PATH module (tripling) to win over the "
        f"in-tree one (doubling)\nActual stdout:\n{result.stdout}"
    )


def test_module_bare_command_name_uses_real_executable_dir(tmp_path):
    """A bare, $PATH-resolved invocation must not resolve modules via cwd.

    Regression test for exactly the bug a naive argv[0]-based
    implementation has: typing a bare command name (no "./", no absolute
    path -- the same shape as running an installed `brainrot` from $PATH)
    gives argv[0] with no directory component at all. Resolving the
    in-tree tier from argv[0]+cwd would then silently search the *caller's*
    current directory instead of the directory the executed binary
    actually lives in.

    Sets up two candidate "stdrot/mathmod.brainrot" modules with different,
    distinguishable content: one next to the real copied binary (via
    $PATH), one in the subprocess's cwd (a decoy). Only the $PATH one may
    win.
    """
    bindir = tmp_path / "bindir"
    bindir.mkdir()
    cwd_dir = tmp_path / "cwd"
    cwd_dir.mkdir()

    _copy_binary(bindir)
    (bindir / "stdrot").mkdir()
    shutil.copy(MATHMOD_SOURCE, bindir / "stdrot" / "mathmod.brainrot")

    (cwd_dir / "stdrot").mkdir()
    shutil.copy(os.path.join(script_dir, "modules_shadow", "mathmod.brainrot"),
                cwd_dir / "stdrot" / "mathmod.brainrot")

    source_path = os.path.abspath(COOKED_MODULE_SEARCH_PATH_SOURCE)

    env = dict(os.environ, PATH=f"{bindir}{os.pathsep}{os.environ.get('PATH', '')}")
    env.pop("BRAINROT_PATH", None)
    env.pop("BRAINROT_TEST_INSTALL_BIN_DIR", None)
    # subprocess.run resolves a slash-free executable name via $PATH itself
    # (like a shell would) while leaving argv[0] as the bare name "brainrot"
    # -- exactly the invocation shape this test exists to cover.
    result = subprocess.run(["brainrot", source_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, cwd=cwd_dir)

    assert result.returncode == 0, (
        f"Expected success, got {result.returncode}\n"
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "42\n", (
        f"Expected the module next to the real executable (doubling) to "
        f"win over cwd's decoy (tripling) -- got:\n{result.stdout}"
    )


def test_module_install_dir_skips_in_tree(tmp_path):
    """Once the running binary IS the install bin dir, in-tree is skipped.

    BRAINROT_TEST_INSTALL_BIN_DIR (module_path.c, test-only seam) stands in
    for the real /usr/local/bin so this doesn't have to write there. With
    the copied binary's own directory treated as "installed", its sibling
    stdrot/mathmod.brainrot must NOT be found -- only the (real, empty in
    this environment) install module directory tier applies -- proving the
    two tiers are mutually exclusive, not just ordered.
    """
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    source_path = os.path.join(
        repo_root, "test_cases", "cooked_module_search_path.brainrot")

    copied_binary = _copy_binary(tmp_path)
    (tmp_path / "stdrot").mkdir()
    shutil.copy(MATHMOD_SOURCE, tmp_path / "stdrot" / "mathmod.brainrot")

    env = dict(os.environ, BRAINROT_TEST_INSTALL_BIN_DIR=str(tmp_path))
    env.pop("BRAINROT_PATH", None)
    env.pop("BRAINROT_TEST_INSTALL_MODULE_DIR", None)
    result = subprocess.run([str(copied_binary), source_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, cwd=repo_root)

    assert result.returncode == 1, (
        f"Expected 'module not found' (in-tree must be skipped once "
        f"treated as installed), got {result.returncode}\n"
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert "cannot find module 'mathmod'" in result.stderr, (
        f"Actual stderr:\n{result.stderr}"
    )


def test_module_install_dir_used_when_installed(tmp_path):
    """The install module dir IS consulted, and wins, once "installed".

    Combines BRAINROT_TEST_INSTALL_BIN_DIR with
    BRAINROT_TEST_INSTALL_MODULE_DIR (both test-only seams standing in for
    /usr/local/bin and /usr/local/lib/brainrot) to prove the positive half
    of the install/in-tree split actually finds a module there -- not just
    that it skips in-tree (test_module_install_dir_skips_in_tree, above).
    """
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    source_path = os.path.join(
        repo_root, "test_cases", "cooked_module_search_path.brainrot")
    fake_install_module_dir = tmp_path / "fake_install_lib_brainrot"
    fake_install_module_dir.mkdir()
    shutil.copy(os.path.join(script_dir, "modules_shadow", "mathmod.brainrot"),
                fake_install_module_dir / "mathmod.brainrot")

    copied_binary = _copy_binary(tmp_path)
    (tmp_path / "stdrot").mkdir()
    shutil.copy(MATHMOD_SOURCE, tmp_path / "stdrot" / "mathmod.brainrot")

    env = dict(os.environ,
              BRAINROT_TEST_INSTALL_BIN_DIR=str(tmp_path),
              BRAINROT_TEST_INSTALL_MODULE_DIR=str(fake_install_module_dir))
    env.pop("BRAINROT_PATH", None)
    result = subprocess.run([str(copied_binary), source_path],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, cwd=repo_root)

    assert result.returncode == 0, (
        f"Expected success, got {result.returncode}\n"
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "63\n", (
        f"Expected the install-dir module (tripling) to win over the "
        f"in-tree one (doubling), which must be skipped entirely -- got:\n"
        f"{result.stdout}"
    )


# ── Native modules: #cooked <name> resolving to a ".so" (stdrot_load_module,
# stdrot.c) ──────────────────────────────────────────────────────────────
# tests/nativemodules/*.c (built by `make nativemodules`) are real modules
# with a genuine brainrot_module_init() entrypoint -- unlike
# tests/badnatives/*.so above (which simulate a malformed CORE
# libstdrot.so, loaded via STDROT_LIB_PATH to exercise stdrot_load()),
# these exercise the #cooked <name>-to-native-module path specifically.
# Driver source is written inline per test (via tmp_path) rather than as
# test_cases/*.brainrot fixtures, since every one of these needs
# $BRAINROT_PATH set -- the generic expected_results.json loop can't
# express that, the same reason the module-search-path tests above don't
# either.
NATIVEMODULES_DIR = os.path.join(script_dir, "nativemodules")


def _run_with_native_modules(source, tmp_path):
    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    source_path = tmp_path / "prog.brainrot"
    source_path.write_text(source)

    env = dict(os.environ, BRAINROT_PATH=NATIVEMODULES_DIR)
    return subprocess.run([brainrot_path, str(source_path)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, env=env)


def _assert_nativemodules_built(*so_names):
    for so in so_names:
        path = os.path.join(NATIVEMODULES_DIR, so)
        assert os.path.exists(path), (
            f"{path} not found -- run `make nativemodules` first")


def test_native_module_dual_load_and_include_once(tmp_path):
    """A native module's exports are callable alongside the core library's,
    and cooking the same module twice is a no-op -- matching a ".brainrot"
    prelude's own include-once behavior (splice_cooked_file, lang.l)."""
    _assert_nativemodules_built("testnative.so")

    result = _run_with_native_modules(
        '#cooked <testnative>\n'
        '#cooked <testnative>\n'
        'skibidi main {\n'
        '    yapping("%d", tripled(2));\n'
        '    bussin 0;\n'
        '}\n', tmp_path)

    assert result.returncode == 0, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "6\n", f"Actual stdout:\n{result.stdout}"


def test_native_module_two_modules_loaded_at_once(tmp_path):
    """Two DIFFERENT, non-colliding native modules are both loaded and both
    remain independently callable -- #207's own "two modules loaded at
    once" DoD item specifically, distinct from the dual-load test above
    (core + one module, one of them cooked twice) and from
    test_native_module_duplicate_with_module below (two modules, but the
    second load must FAIL)."""
    _assert_nativemodules_built("testnative.so", "testnative2.so")

    result = _run_with_native_modules(
        '#cooked <testnative>\n'
        '#cooked <testnative2>\n'
        'skibidi main {\n'
        '    yapping("%d", tripled(2));\n'
        '    yapping("%d", halved(10));\n'
        '    bussin 0;\n'
        '}\n', tmp_path)

    assert result.returncode == 0, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert result.stdout == "6\n5\n", f"Actual stdout:\n{result.stdout}"


def test_native_module_duplicate_with_core(tmp_path):
    """A module exporting a name the core library already provides ('bet')
    must be rejected, naming the core library as the existing source."""
    _assert_nativemodules_built("testnative_dup_core.so")

    result = _run_with_native_modules(
        '#cooked <testnative_dup_core>\n'
        'skibidi main { bussin 0; }\n', tmp_path)

    assert result.returncode == 1, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert ("'bet' is already provided by the core standard library"
            in result.stderr), f"Actual stderr:\n{result.stderr}"


def test_native_module_duplicate_with_module(tmp_path):
    """A module exporting a name an EARLIER cooked module already provides
    must be rejected too, naming that earlier module (by its #cooked <name>
    spelling) as the existing source -- not just the core library."""
    _assert_nativemodules_built("testnative.so", "testnative_dup_module.so")

    result = _run_with_native_modules(
        '#cooked <testnative>\n'
        '#cooked <testnative_dup_module>\n'
        'skibidi main { bussin 0; }\n', tmp_path)

    assert result.returncode == 1, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert "'tripled' is already provided by testnative" in result.stderr, (
        f"Actual stderr:\n{result.stderr}"
    )


def test_native_module_missing_brainrot_module_init(tmp_path):
    """A structurally valid .so with no brainrot_module_init() at all must
    fail loudly and specifically, the same dlsym-failure posture
    stdrot_load() already has for a pre-ABI-versioning libstdrot.so (see
    test_old_abi_rejected_at_load above)."""
    _assert_nativemodules_built("no_module_init.so")

    result = _run_with_native_modules(
        '#cooked <no_module_init>\n'
        'skibidi main { bussin 0; }\n', tmp_path)

    assert result.returncode == 1, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert "does not export brainrot_module_init()" in result.stderr, (
        f"Actual stderr:\n{result.stderr}"
    )


def test_native_module_internal_duplicate_rejected(tmp_path):
    """stdrot_load_module() (stdrot.c) runs validate_native_registry() on a
    cooked module's own table -- the same rejection
    test_bad_registry_rejected_at_load above already proves for the core
    library's table, exercised here via the module-loading path instead."""
    _assert_nativemodules_built("testnative_internal_dup.so")

    result = _run_with_native_modules(
        '#cooked <testnative_internal_dup>\n'
        'skibidi main { bussin 0; }\n', tmp_path)

    assert result.returncode == 1, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}"
    )
    assert "duplicate native export 'dup_within_module'" in result.stderr, (
        f"Actual stderr:\n{result.stderr}"
    )


REPO_ROOT = os.path.abspath(os.path.join(script_dir, ".."))
BRAINRAY_DIR = os.path.join(REPO_ROOT, "brainray")


def _raylib_available():
    """True when pkg-config can find raylib, i.e. `make brainray` can build
    the optional binding. raylib is not a dependency of `make test`, so when
    it is absent the brainray test below skips (with a reason) rather than
    failing -- matching #208's "make test does not require raylib"."""
    if shutil.which("pkg-config") is None:
        return False
    return subprocess.run(
        ["pkg-config", "--exists", "raylib"]).returncode == 0


@pytest.mark.skipif(
    not _raylib_available(),
    reason="raylib not installed (pkg-config --exists raylib failed); "
           "brainray is an optional dependency, not required by make test")
def test_brainray_module_loads_when_raylib_present(tmp_path):
    """When raylib IS present, `make brainray` builds brainray/raylib.so and
    `#cooked <raylib>` loads it end to end. This proves the module exports
    brainrot_module_init(), the module search path resolves the native `.so`,
    and every rl_* arity/type descriptor passes validate_native_registry() at
    load time. It calls no rl_* function, so it needs no window or display --
    the load itself (dlopen at parse time) is what is under test."""
    build = subprocess.run(
        ["make", "brainray"], cwd=REPO_ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    assert build.returncode == 0, f"`make brainray` failed:\n{build.stdout}"
    assert os.path.exists(os.path.join(BRAINRAY_DIR, "raylib.so")), (
        "make brainray did not produce brainray/raylib.so")

    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    source_path = tmp_path / "prog.brainrot"
    source_path.write_text("#cooked <raylib>\nskibidi main { bussin 0; }\n")

    env = dict(os.environ, BRAINROT_PATH=BRAINRAY_DIR)
    result = subprocess.run(
        [brainrot_path, str(source_path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)

    assert result.returncode == 0, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}")


@pytest.mark.skipif(
    not _raylib_available() or not os.environ.get("DISPLAY"),
    reason="needs raylib AND a display ($DISPLAY): raylib is optional and the "
           "windowed run cannot open a window in headless CI")
def test_brainray_windowed_run_is_leak_clean(tmp_path):
    """The windowed demo must exit clean under the default (ASan) build --
    the regression guard for issue #267. brainray brackets raylib's own calls
    with __lsan_disable/__lsan_enable so the graphics stack's process-lifetime
    globals are not reported, while brainray's own allocations (the window
    title, the texture table) stay tracked. A leak on either side -- a real
    brainray leak, or the bracketing being removed so raylib's globals surface
    again -- makes ASan exit nonzero and prints "LeakSanitizer", failing here.

    The program calls rl_draw_text_int/rl_measure_text_int on purpose: they are
    the only wrappers besides rl_init_window that allocate, so a missing free()
    in br_format_text_int() has to be visible somewhere, and this is that
    somewhere. Both are called every iteration so a per-call leak accumulates.

    Uses a frame-capped program (the shipped example loops until the window is
    closed) so the run terminates on its own. Skips without raylib or a
    display, so headless CI never runs it."""
    build = subprocess.run(
        ["make", "brainray"], cwd=REPO_ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    assert build.returncode == 0, f"`make brainray` failed:\n{build.stdout}"

    brainrot_path = os.path.abspath(os.path.join(script_dir, "../brainrot"))
    source_path = tmp_path / "leak_smoke.brainrot"
    source_path.write_text(
        "#cooked <raylib>\n"
        "skibidi main {\n"
        "    rl_init_window(160, 120, \"leak smoke\");\n"
        "    rizz n = 0;\n"
        "    cap running = W;\n"
        "    goon (running) {\n"
        "        cap down = rl_is_key_down(32);\n"
        "        rl_begin_drawing();\n"
        "        rl_clear_background(20, 20, 20, 255);\n"
        "        rl_draw_circle(80, 60, 20.0, 255, 0, 255, 255);\n"
        "        rl_draw_text(\"cinema\", 10, 10, 16, 255, 255, 255, 255);\n"
        "        rizz w = rl_measure_text_int(\"n \", n, 4, 16);\n"
        "        rl_draw_text_int(\"n \", n, 4, w, 30, 16, 255, 255, 0, 255);\n"
        "        rl_end_drawing();\n"
        "        cap wc = rl_window_should_close();\n"
        "        edgy (wc) { running = L; }\n"
        "        n = n + 1;\n"
        "        edgy (n > 5) { running = L; }\n"
        "    }\n"
        "    rl_close_window();\n"
        "    bussin 0;\n"
        "}\n")

    # Default env: leak detection stays ON (no ASAN_OPTIONS override). A leak
    # would make ASan exit nonzero; assert the clean exit and no LSan report.
    env = dict(os.environ, BRAINROT_PATH=BRAINRAY_DIR)
    result = subprocess.run(
        [brainrot_path, str(source_path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env,
        timeout=60)

    assert "LeakSanitizer" not in result.stderr, (
        f"LeakSanitizer reported leaks on the windowed run:\n{result.stderr}")
    assert result.returncode == 0, (
        f"Nonzero exit {result.returncode}\n"
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}")


if __name__ == "__main__":
    pytest.main(["-v", os.path.abspath(__file__)])
