"""Tests for rayrot/rayrot_gen.py -- the raylib binding generator
(issue #208, Phase 5 Road B).

None of these need raylib installed. That is the point: `make test` must stay
green on a machine with no raylib, so everything verifiable from the pinned
rayrot/raylib_api.json alone is verified here, and only the two things that
genuinely require raylib's headers are left to the opt-in `make rayrot-gen`
target:

  * compiling the generated adapters, and
  * the generated _Static_assert/offsetof translation unit, which is what
    actually compares the generator's computed layouts against real raylib.

What IS covered without raylib is the more interesting half of the layout
story anyway: that the generator's own layout model agrees with Brainrot's
compute_struct_layout(). Those are two independent implementations of C's
struct rules -- one in Python here, one in ast.c -- compared by making the
interpreter itself report sizes, in two steps that are both needed:

  * ..._gang_layouts_match_brainrot() compares each type's TOTAL size.
  * ..._gang_field_offsets_match_brainrot() compares INTERIOR padding, via
    prefix probes with a trailing 1-byte sentinel. Total size alone is not
    enough and that is not hypothetical: a `RayCollision` with no interior
    padding whatsoever still totals 32 bytes, so the first test passes on a
    layout with every field in the wrong place (PR #307 review, finding 3).

Be precise about the strength of each edge, because an earlier version of
this docstring was not. The generated ABI translation unit compares the
generator's model to real raylib headers EXACTLY -- `sizeof`, `_Alignof`, and
every single `offsetof`. The generator↔Brainrot comparison here is strong but
indirect: Brainrot exposes no way to observe a field's address, so offsets are
compared through the sizes of prefix structs rather than read off directly. It
is sensitive to any interior-padding disagreement (verified by mutation), not
to literally every conceivable byte-level divergence.
"""

import json
import os
import subprocess
import sys

import pytest

script_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.dirname(script_dir)
rayrot_dir = os.path.join(repo_root, "rayrot")
API_PATH = os.path.join(rayrot_dir, "raylib_api.json")

sys.path.insert(0, rayrot_dir)
import rayrot_gen as gen  # noqa: E402  (needs the path insert above)


@pytest.fixture(scope="module")
def api():
    parsed, repaired = gen.load_api(API_PATH)
    return parsed, repaired


@pytest.fixture(scope="module")
def model(api):
    m = gen.TypeModel(api[0])
    m.compute_layouts()
    return m


def test_vendored_api_is_valid_json_as_committed():
    """The vendored file must be usable as-is. load_api() carries a repair for
    a real upstream defect (raylib's own 6.0 tag ships malformed JSON), but the
    copy we pin is a good one -- if this starts failing, someone re-vendored
    from a broken tag."""
    with open(API_PATH, encoding="utf-8") as fh:
        json.load(fh)


def test_vendored_api_needs_no_repair(api):
    _, repaired = api
    assert repaired == 0, (
        f"the pinned raylib_api.json needed {repaired} description repair(s) "
        "-- it should be a well-formed upstream copy; see load_api()")


def test_load_api_repairs_unescaped_quotes_in_description(tmp_path):
    """Exercises the upstream-defect workaround directly, since the pinned
    file (correctly) does not trigger it. The malformed shape is verbatim what
    raylib's 6.0 tag ships: unescaped double quotes inside a description."""
    broken = tmp_path / "broken_api.json"
    broken.write_text(
        '{\n'
        '  "functions": [\n'
        '    {\n'
        '      "name": "LoadDirectoryFilesEx",\n'
        '      "description": "filters available: "*.*", "DIRS*"",\n'
        '      "returnType": "void",\n'
        '      "params": []\n'
        '    }\n'
        '  ]\n'
        '}\n', encoding="utf-8")

    parsed, repaired = gen.load_api(str(broken))
    assert repaired == 1
    fn = parsed["functions"][0]
    # The repair must preserve the text and, crucially, leave every
    # structural field untouched -- it only ever rewrites description values.
    assert fn["name"] == "LoadDirectoryFilesEx"
    assert fn["returnType"] == "void"
    assert '"*.*"' in fn["description"]


def test_load_api_refuses_to_guess_at_other_json_errors(tmp_path):
    """A malformation that is NOT the known description defect must fail
    loudly rather than get papered over -- the repair is narrow on purpose."""
    broken = tmp_path / "truncated.json"
    broken.write_text('{"functions": [{"name": "Foo"', encoding="utf-8")
    with pytest.raises(SystemExit) as exc:
        gen.load_api(str(broken))
    assert "invalid JSON" in str(exc.value)


@pytest.mark.parametrize("camel,expected", [
    ("DrawCircleV", "draw_circle_v"),
    ("InitWindow", "init_window"),
    ("DrawFPS", "draw_fps"),                     # acronym stays one word
    ("LoadImageFromTexture", "load_image_from_texture"),
    ("GetShaderLocation", "get_shader_location"),
    ("IsKeyDown", "is_key_down"),
    ("UnloadVrStereoConfig", "unload_vr_stereo_config"),
    ("CheckCollisionPointRec", "check_collision_point_rec"),
    ("GetRandomValue", "get_random_value"),
])
def test_snake_case_conversion(camel, expected):
    assert gen.snake(camel) == expected


# Known-good raylib layouts on any LP64/ILP32 target this repo builds for --
# these structs are all-float or all-byte, so they do not vary by data model.
# Hardcoded rather than read from the JSON so a generator bug cannot make the
# test agree with itself.
KNOWN_LAYOUTS = {
    "Vector2": (8, 4, {"x": 0, "y": 4}),
    "Vector3": (12, 4, {"x": 0, "y": 4, "z": 8}),
    "Vector4": (16, 4, {"x": 0, "y": 4, "z": 8, "w": 12}),
    "Color": (4, 1, {"r": 0, "g": 1, "b": 2, "a": 3}),
    "Rectangle": (16, 4, {"x": 0, "y": 4, "width": 8, "height": 12}),
    "Matrix": (64, 4, {"m0": 0, "m5": 20, "m15": 60}),
    "Ray": (24, 4, {"position": 0, "direction": 12}),
    "BoundingBox": (24, 4, {"min": 0, "max": 12}),
}


@pytest.mark.parametrize("name", sorted(KNOWN_LAYOUTS))
def test_computed_layout_matches_known_raylib_layout(model, name):
    expected_size, expected_align, expected_offsets = KNOWN_LAYOUTS[name]
    assert name in model.layouts, f"{name} should be emittable"
    size, align, fields = model.layouts[name]
    assert (size, align) == (expected_size, expected_align)
    offsets = {fname: off for fname, off, _ in fields}
    for fname, off in expected_offsets.items():
        assert offsets[fname] == off, f"{name}.{fname}"


def test_structs_with_pointers_or_arrays_are_not_emittable(model):
    """These are resource handles in disguise (they own or point at memory),
    so they are deliberately excluded rather than byte-copied -- see roadmap
    Appendix B Q6."""
    for name in ("Image", "Font", "Model", "Sound", "Mesh", "Shader", "Wave"):
        assert name in model.structs, f"{name} missing from the API entirely"
        assert name not in model.layouts, (
            f"{name} holds pointers/arrays and must not be emitted as a "
            "by-value gang")


def test_struct_returns_are_skipped_with_that_reason(model):
    """STDROT_STRUCT is argument-direction only, so a struct-returning
    function must be skipped -- and skipped for a stated reason, not silently
    mapped to something wrong."""
    kind, reason = model.return_abi("Vector2")
    assert kind == "skip" and reason == "struct return"
    kind, reason = model.return_abi("const char *")
    assert kind == "skip" and reason == "cstring return"


def test_by_value_struct_params_get_a_tag(model):
    """A STDROT_STRUCT descriptor without a type_name is rejected at load
    time by validate_native_registry(), so the generator must always emit
    one."""
    stype, tname, level, _ = model.param_abi("Vector2")
    assert (stype, tname, level) == ("STDROT_STRUCT", "Vector2", 0)
    # Texture2D is a typedef of Texture -- the tag must be the resolved name,
    # since that is what the prelude declares as a `gang`.
    stype, tname, _, _ = model.param_abi("Texture2D")
    assert (stype, tname) == ("STDROT_STRUCT", "Texture")


@pytest.fixture(scope="module")
def generated(tmp_path_factory):
    """Run the generator exactly as `make rayrot-gen-sources` does,
    including --strict, and hand back the output directory."""
    out = tmp_path_factory.mktemp("rayrot_generated")
    rc = gen.main(["--api", API_PATH, "--outdir", str(out), "--strict"])
    assert rc == 0
    return out


def test_generator_runs_strict_on_the_pinned_api(generated):
    """--strict fails on any skip reason that isn't a known, documented ABI
    gap, so this passing means the pinned JSON still has the shape the
    generator understands. This is the test that turns an upstream schema
    change into a red build instead of a quietly smaller binding."""
    for name in ("raylibgen_native.c", "raylibgen.brainrot",
                 "raylibgen_abi_check.c"):
        path = generated / name
        assert path.exists() and path.stat().st_size > 0, name


def test_generated_native_covers_the_game_loop(generated):
    """A binding that omits the functions an actual game loop needs would be
    coverage theater. These are exactly the calls
    examples/raylib/ohio_engine_gen.brainrot makes, plus the by-value-struct
    ones that are the whole reason Road B needed a new ABI."""
    src = (generated / "raylibgen_native.c").read_text(encoding="utf-8")
    for fn in ("rl_init_window", "rl_close_window", "rl_begin_drawing",
               "rl_end_drawing", "rl_clear_background", "rl_draw_circle_v",
               "rl_draw_rectangle_rec", "rl_draw_text",
               "rl_window_should_close", "rl_set_target_fps",
               "rl_is_key_down", "rl_get_frame_time",
               "rl_check_collision_recs", "rl_color_to_int"):
        assert f'"{fn}"' in src, f"{fn} not exported by the generated binding"


def test_generated_adapters_read_structs_through_the_size_checked_macro(
        generated):
    """Every by-value struct read must go through BR_READ_STRUCT, which
    verifies .val.blob.size against sizeof before memcpy'ing -- a raw memcpy
    would turn a host/module layout disagreement into an out-of-bounds read
    inside raylib."""
    src = (generated / "raylibgen_native.c").read_text(encoding="utf-8")
    assert "#define BR_READ_STRUCT" in src
    # DrawCircleV takes Vector2 and Color by value and a float between them.
    assert "BR_READ_STRUCT(p0_center, args[0], Vector2);" in src
    assert "BR_READ_STRUCT(p2_color, args[2], Color);" in src
    # No adapter should memcpy straight out of a blob.
    assert "memcpy(&" not in src.split("#define BR_READ_STRUCT", 1)[1].split(
        "static void br_struct_size_mismatch", 1)[1], (
        "an adapter appears to memcpy a struct argument without the size check")


def test_generated_adapters_bracket_library_calls_for_lsan(generated):
    """raylib's process-lifetime globals would otherwise be reported as leaks
    by the sanitizer build (issue #267). The bracket must wrap the library
    call only, so interpreter-side leaks stay visible."""
    src = (generated / "raylibgen_native.c").read_text(encoding="utf-8")
    assert "__lsan_disable" in src and "__lsan_enable" in src
    assert src.count("br_lsan_ignore_begin();") == src.count(
        "br_lsan_ignore_end();"), "unbalanced LSan brackets"


def test_generated_prelude_cooks_the_native_module(generated):
    """The prelude is what `#cooked <raylibgen>` actually resolves to (a
    "<name>.brainrot" wins over a "<name>.so"), so it has to pull in the
    native half itself."""
    prelude = (generated / "raylibgen.brainrot").read_text(encoding="utf-8")
    assert "#cooked <raylibgen_native>" in prelude
    assert "gang Vector2 {" in prelude
    assert "gang Color {" in prelude
    # Constants must be emitted as gyatt enums with their real values.
    assert "gyatt " in prelude
    assert "KEY_SPACE = 32" in prelude


def test_generated_constants_are_globally_unique(generated):
    """Brainrot enum constants share one global namespace, so a duplicate
    would make the prelude fail to parse. build_constants() dedupes; this
    proves it actually did."""
    prelude = (generated / "raylibgen.brainrot").read_text(encoding="utf-8")
    names = []
    for line in prelude.splitlines():
        stripped = line.strip()
        if "=" in stripped and not stripped.startswith("🚽") \
                and not stripped.startswith("gyatt") \
                and not stripped.startswith("gang"):
            names.append(stripped.split("=", 1)[0].strip())
    assert len(names) == len(set(names)), (
        f"duplicate constant(s): "
        f"{sorted({n for n in names if names.count(n) > 1})}")


def test_generated_gang_layouts_match_brainrot(generated, tmp_path):
    """The cross-check that needs no raylib: does Brainrot's own
    compute_struct_layout() (ast.c) agree with the generator's Python layout
    model?

    Two independent implementations of C's struct rules, compared by having
    the interpreter report maxxing() for every generated `gang`. The prelude's
    `#cooked <raylibgen_native>` is stripped, because loading the native half
    is what would require raylib -- the type declarations alone are pure
    Brainrot source and stand on their own.
    """
    brainrot = os.path.join(repo_root, "brainrot")
    assert os.path.exists(brainrot), "run `make` first"

    prelude = (generated / "raylibgen.brainrot").read_text(encoding="utf-8")
    types_only = "\n".join(l for l in prelude.splitlines()
                           if not l.startswith("#cooked"))

    # Recover (name -> expected size) from the generator's own comments.
    expected = {}
    for line in prelude.splitlines():
        if line.startswith("🚽 ") and " bytes, align " in line:
            head = line[2:].split(":", 1)
            expected[head[0].strip()] = int(
                head[1].strip().split(" bytes")[0])
    assert len(expected) >= 16, f"only found {len(expected)} gang sizes"

    names = sorted(expected)
    body = ["skibidi main {"]
    for i, name in enumerate(names):
        body.append(f"    gang {name} v{i};")
        body.append(f'    yapping("{name} %lu", maxxing(v{i}));')
    body.append("    bussin 0;")
    body.append("}")

    program = tmp_path / "layout_probe.brainrot"
    program.write_text(types_only + "\n" + "\n".join(body) + "\n",
                       encoding="utf-8")

    result = subprocess.run([brainrot, str(program)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    assert result.returncode == 0, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}")

    actual = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            actual[parts[0]] = int(parts[1])

    mismatches = {n: (expected[n], actual.get(n)) for n in names
                  if actual.get(n) != expected[n]}
    assert not mismatches, (
        "generator layout model disagrees with Brainrot's "
        f"compute_struct_layout(): {mismatches}")


def _field_spelling(ctype, fname):
    """How the prelude declares one field -- a Brainrot scalar keyword, or
    `gang <Tag>` for a nested by-value struct. Mirrors emit_prelude()."""
    if ctype in gen.SCALARS:
        return f"{gen.SCALARS[ctype][1]} {fname};"
    return f"gang {ctype} {fname};"


def test_generated_gang_field_offsets_match_brainrot(generated, model,
                                                     tmp_path):
    """The interior half of the generator↔Brainrot layout comparison.

    test_generated_gang_layouts_match_brainrot() above compares total size,
    which is necessary but genuinely not sufficient: `RayCollision` is
    `{cap; chad; gang Vector3; gang Vector3;}`, and a layout that applied NO
    interior padding at all would put its fields at 0/1/5/17 and still total
    32 bytes after rounding to alignment 4 -- identical `maxxing()`, every
    offset wrong, and `DrawRay`-shaped calls quietly reading garbage
    (PR #307 review, finding 3). `BR_READ_STRUCT` checks size too, so it
    cannot catch it either.

    Comparing offsets directly would need a way to observe an address from
    Brainrot. Instead this compares the two algorithms at every interior
    step, which is the same information: for each struct it declares a
    PREFIX gang holding just the first k fields and asks the interpreter for
    its size. That size is a direct function of the padding decisions
    between exactly those fields, so if the generator and
    compute_struct_layout() agree on every prefix of every struct, they agree
    about where each field lands. On the packed-RayCollision hypothetical the
    prefix `{cap; chad;}` is 8 bytes correctly padded and 5 packed -- caught.
    """
    brainrot = os.path.join(repo_root, "brainrot")
    assert os.path.exists(brainrot), "run `make` first"

    prelude = (generated / "raylibgen.brainrot").read_text(encoding="utf-8")
    types_only = "\n".join(l for l in prelude.splitlines()
                           if not l.startswith("#cooked"))

    # (probe gang name -> size the GENERATOR computes for that field list)
    expected = {}
    decls = []
    for name in model.emittable:
        _, _, fields = model.layouts[name]
        if len(fields) < 2:
            continue  # a one-field struct has no interior to disagree about
        for k in range(1, len(fields)):
            prefix = fields[:k]
            probe = f"Pfx{name}{k}"
            # A trailing 1-byte sentinel is what makes this probe see
            # INTERIOR padding at all. Without it the struct's size is
            # rounded up to its own max alignment, which hides the very
            # thing being measured: a packed RayCollision prefix
            # `{cap; chad;}` ends at 5 and a correctly padded one at 8,
            # but both round to 8. Appending `yap` (size 1, align 1)
            # places a field at exactly the prefix's end, so the size
            # becomes a function of that end rather than of the rounding.
            fields_with_sentinel = [{"name": fname, "type": ctype}
                                    for fname, _, ctype in prefix]
            fields_with_sentinel.append({"name": "probe_end",
                                         "type": "char"})
            layout = model._try_layout({"fields": fields_with_sentinel})
            assert layout is not None, f"{probe} should be computable"
            expected[probe] = layout[0]
            decls.append(f"gang {probe} {{")
            decls.extend("    " + _field_spelling(ctype, fname)
                         for fname, _, ctype in prefix)
            decls.append("    yap probe_end;")
            decls.append("};")

    assert len(expected) >= 20, (
        f"only built {len(expected)} prefix probes -- expected the 16 "
        "generated structs to yield many more")

    probes = sorted(expected)
    body = ["skibidi main {"]
    for i, probe in enumerate(probes):
        body.append(f"    gang {probe} v{i};")
        body.append(f'    yapping("{probe} %lu", maxxing(v{i}));')
    body.append("    bussin 0;")
    body.append("}")

    program = tmp_path / "offset_probe.brainrot"
    program.write_text(
        types_only + "\n" + "\n".join(decls) + "\n" + "\n".join(body) + "\n",
        encoding="utf-8")

    result = subprocess.run([brainrot, str(program)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    assert result.returncode == 0, (
        f"Stdout:\n{result.stdout}\nStderr:\n{result.stderr}")

    actual = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) == 2:
            actual[parts[0]] = int(parts[1])

    mismatches = {p: (expected[p], actual.get(p)) for p in probes
                  if actual.get(p) != expected[p]}
    assert not mismatches, (
        "generator and compute_struct_layout() disagree about interior "
        f"padding (probe: (generator, brainrot)): {mismatches}")
