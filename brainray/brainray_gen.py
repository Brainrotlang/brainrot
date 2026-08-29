#!/usr/bin/env python3
"""brainray-gen -- generate a Brainrot native binding from a C library's
machine-readable API description (issue #208, Phase 5 Road B).

    raylib_api.json --> brainray-gen --> { C adapters + StdrotEntry
                                           descriptors, a Brainrot prelude
                                           of gang types and gyatt
                                           constants, ABI drift tests }

WHY THIS EXISTS
raylib has 600 functions. Hand-writing 600 StdrotValue wrappers is how this
project dies -- so the wrappers are emitted from raylib's own published
description of its API, and raylib becomes the first client of a pipeline
rather than a one-off port. Nothing here is raylib-specific except the
vendored JSON and a handful of names in the CLI defaults; see
"POINTING THIS AT A DIFFERENT LIBRARY" at the bottom.

WHAT IT EMITS (three files, all derived, none committed -- roadmap
Appendix B Q7)
  <out>/raylibgen_native.c      C adapters + STDROT_EXPORT_SIG descriptors
  <out>/raylibgen.brainrot      prelude: gang types, gyatt constants, and a
                                #cooked of the native module above
  <out>/raylibgen_abi_check.c   _Static_assert/offsetof against real raylib
                                headers, asserting the layout numbers THIS
                                generator computed

The prelude is why this needs no new ABI machinery for types or constants.
`StdrotAPI` carries a function table and nothing else, and Phase 4 left
type/constant registration deliberately undesigned -- but a module name
resolves to a "<name>.brainrot" prelude BEFORE a "<name>.so"
(module_path_resolve(), lib/module_path.c), and a prelude may itself
`#cooked` a native module. So `gang Vector2 { chad x; chad y; }` and
`gyatt KeyboardKey { KEY_SPACE = 32, ... }` ship as ordinary generated
Brainrot source, and only the functions need the C ABI.

WHAT IT DELIBERATELY SKIPS
Every skip is counted and reported, and `--strict` turns any UNEXPECTED
skip reason into a failure. A binding that quietly omits half a library is
worse than one that says what it left out:
  * struct returns -- STDROT_STRUCT is argument-direction only, because
    returning an aggregate needs an ownership model this ABI hasn't chosen
    (roadmap Appendix B Q6). This is the single biggest cost: it drops
    every `Vector2 Vector2Add(...)`-shaped function.
  * `const char *` returns -- no return-side marshalling exists for a C
    string (semantic_check_native_call() rejects the declared type).
  * structs holding pointers or arrays (Image, Font, Model, Sound, ...) --
    these are resource handles in disguise; they want Appendix B Q6's
    answer too, not a byte copy.
  * C types with no layout-identical Brainrot spelling, `long` most
    notably: 8 bytes on LP64 against `rizz`'s 4.
  * varargs and function-pointer/callback parameters.

POINTING THIS AT A DIFFERENT LIBRARY
The library-independent parts are SCALARS (the C-scalar-to-Brainrot map),
the layout computation, the adapter/descriptor emitters, and the skip
bookkeeping. To bind SDL or SQLite instead you need: a JSON description in
the same shape (name/description/returnType/params for functions,
name/fields for structs, name/values for enums -- see load_api()), and the
right --header/--module-name/--prefix flags. What you should NOT need to
touch is the emission logic; if you do, that is a bug in the abstraction,
not in your library.
"""

import argparse
import json
import re
import sys
from collections import Counter, OrderedDict

# ── The C-to-Brainrot scalar map ─────────────────────────────────────────
# (StdrotType, Brainrot field keyword, size, alignment).
#
# A C type appears here ONLY if its Brainrot spelling has an identical size
# and alignment, because these entries are used for two different jobs that
# both depend on that: choosing a StdrotParam type, and computing the
# `gang` layout the prelude declares. `long` is the instructive omission --
# `giga rizz` is 8 bytes on LP64 and 4 on ILP32/wasm32, so a struct
# containing one has no single layout to emit and any function taking one
# is skipped instead of silently truncated.
#
# The two signedness pairs (`unsigned char`/`char`, `unsigned int`/`int`)
# map to the same Brainrot spelling on purpose: Brainrot has no unsigned
# struct-field spelling (`nonut yap r;` is a parse error), and the BYTES
# are what cross the ABI, which are identical. The visible consequence is
# on the Brainrot side only -- a `Color` component set to 200 reads back as
# -56 through a signed `yap` -- and it is documented rather than worked
# around, because working around it would mean changing the field's size
# and breaking the layout this whole binding depends on.
SCALARS = OrderedDict([
    ("bool", ("STDROT_BOOL", "cap", 1, 1)),
    ("char", ("STDROT_CHAR", "yap", 1, 1)),
    ("unsigned char", ("STDROT_CHAR", "yap", 1, 1)),
    ("short", ("STDROT_SHORT", "smol", 2, 2)),
    ("unsigned short", ("STDROT_SHORT", "smol", 2, 2)),
    ("int", ("STDROT_INT", "rizz", 4, 4)),
    ("unsigned int", ("STDROT_INT", "rizz", 4, 4)),
    ("float", ("STDROT_FLOAT", "chad", 4, 4)),
    ("double", ("STDROT_DOUBLE", "gigachad", 8, 8)),
])

# How an adapter reads each StdrotType back out of a StdrotValue. Kept
# beside SCALARS so a new scalar cannot be added without also saying how to
# read it.
UNION_MEMBER = {
    "STDROT_BOOL": "b",
    "STDROT_CHAR": "c",
    "STDROT_SHORT": "s",
    "STDROT_INT": "i",
    "STDROT_FLOAT": "f",
    "STDROT_DOUBLE": "d",
    "STDROT_CSTRING": "cstr",
    "STDROT_PTR": "ptr",
}

# Skip reasons this generator EXPECTS to hit on a healthy input -- each one
# is a documented ABI gap or a deliberate conservatism, not a surprise.
# Anything outside this set is what --strict fails on, so a schema change
# upstream (a new type spelling, a renamed field) surfaces as a build
# failure instead of a quietly smaller binding.
EXPECTED_SKIPS = {
    "struct return",
    "cstring return",
    "unsupported return type",
    "unsupported param type",
    "unsupported struct field",
    "varargs",
    "callback param",
    # Brainrot enum constants share one global namespace, so a name used by
    # two enums has to be dropped rather than shadowed -- deliberate, and
    # reported, but not a schema surprise.
    "duplicate constant",
}


class Skips:
    """Counts skips by reason and keeps a couple of examples of each, so
    the report can say *which* functions went missing rather than only how
    many."""

    def __init__(self):
        self.counts = Counter()
        self.examples = {}
        self.detail = Counter()

    def add(self, reason, name, detail=None):
        self.counts[reason] += 1
        self.examples.setdefault(reason, []).append(name)
        if detail:
            self.detail[f"{reason}: {detail}"] += 1

    def unexpected(self):
        return sorted(set(self.counts) - EXPECTED_SKIPS)


def load_api(path):
    """Parse the vendored API description.

    The single repair below works around a real defect in raylib's own
    published 6.0 artifact: one `description` value contains unescaped
    double quotes ("...some filters available: "*.*", "FILES*", "DIRS*""),
    which makes the whole file invalid JSON. The 5.5, 5.0 and master
    artifacts are all well-formed, so this is specific to that tag.

    Repairing here rather than in the vendored file is deliberate: the
    committed raylib_api.json stays BYTE-IDENTICAL to upstream, so
    re-vendoring is a plain download with no manual editing step to forget,
    and diffing it against upstream stays meaningful. The repair is
    confined to `description` values -- documentation text this generator
    only ever passes through into comments -- so it cannot alter a type, a
    name, or an arity. If upstream fixes the file, this simply stops
    matching and does nothing.
    """
    raw = open(path, encoding="utf-8").read()
    try:
        return json.loads(raw), 0
    except json.JSONDecodeError:
        pass

    repaired = 0
    lines = raw.splitlines()
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith('"description":'):
            continue
        value = stripped[len('"description":'):].strip().rstrip(",")
        if value.count('"') <= 2 or not (
                value.startswith('"') and value.endswith('"')):
            continue
        head, _, tail = line.partition('"description":')
        inner = value[1:-1].replace('"', '\\"')
        lines[i] = f'{head}"description": "{inner}"' + (
            "," if stripped.endswith(",") else "")
        repaired += 1

    if not repaired:
        raise SystemExit(
            f"{path}: invalid JSON, and no malformed `description` value was "
            "found to repair. The upstream schema probably changed -- inspect "
            "the file rather than loosening this parser.")
    try:
        return json.loads("\n".join(lines)), repaired
    except json.JSONDecodeError as exc:
        raise SystemExit(
            f"{path}: still invalid JSON after repairing {repaired} "
            f"description value(s): {exc}")


def snake(name):
    """CamelCase -> snake_case, matching the `rl_*` naming Road A
    established (brainray/raylib.c): DrawCircleV -> draw_circle_v,
    DrawFPS -> draw_fps, LoadImageFromTexture -> load_image_from_texture.

    Runs of capitals are treated as one word so acronyms don't explode into
    single letters (FPS, VR, AABB). Collisions are not tolerated silently
    -- see build_functions(), which fails on one rather than letting two
    raylib functions land on the same Brainrot name and have
    validate_native_registry() reject the whole module at load time with a
    duplicate-export error nobody can trace back to here.
    """
    out = re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name)
    out = re.sub(r"(?<=[A-Z])([A-Z][a-z])", r"_\1", out)
    return out.lower()


class TypeModel:
    """Resolves the library's type names into ABI decisions and layouts."""

    def __init__(self, api):
        self.structs = OrderedDict((s["name"], s) for s in api.get("structs", []))
        self.enums = OrderedDict((e["name"], e) for e in api.get("enums", []))
        self.callbacks = {c["name"] for c in api.get("callbacks", [])}
        # A leading '*' in an alias name is rlparser noise for a pointer
        # typedef; those alias a type we would only ever treat as opaque
        # anyway, so drop them rather than resolve through them.
        self.aliases = {a["name"]: a["type"] for a in api.get("aliases", [])
                        if not a["name"].startswith("*")}
        self.layouts = {}      # struct name -> (size, align, [(field, off)])
        self.emittable = []    # struct names, in dependency order

    def resolve(self, ctype):
        """Follow typedef aliases (Texture2D -> Texture) to a fixed point."""
        seen = set()
        while ctype in self.aliases and ctype not in seen:
            seen.add(ctype)
            ctype = self.aliases[ctype]
        return ctype

    def compute_layouts(self):
        """Find every struct whose layout this generator can state exactly,
        and compute it with C's own rules.

        Iterated to a fixed point rather than done in one pass because a
        struct is only emittable if the structs it embeds by value already
        are (Rectangle inside NPatchInfo, Vector3 inside BoundingBox), and
        the JSON does not order declarations for us. The loop terminates
        because each round either adds a struct or stops.

        Anything with a pointer or array field is left out on purpose: a
        `float *` or `char[32]` field means the struct owns or points at
        memory, which makes it a resource whose ownership Appendix B Q6
        governs -- not something to hand across the boundary as a flat byte
        copy.
        """
        pending = dict(self.structs)
        progress = True
        while progress:
            progress = False
            for name in list(pending):
                layout = self._try_layout(pending[name])
                if layout is None:
                    continue
                self.layouts[name] = layout
                self.emittable.append(name)
                del pending[name]
                progress = True
        return pending  # whatever never became computable

    def _try_layout(self, struct):
        offset = 0
        max_align = 1
        fields = []
        for field in struct.get("fields", []):
            ctype = self.resolve(field["type"])
            if ctype in SCALARS:
                _, _, size, align = SCALARS[ctype]
            elif ctype in self.layouts:
                size, align, _ = self.layouts[ctype]
            else:
                return None
            offset = _align_up(offset, align)
            fields.append((field["name"], offset, ctype))
            offset += size
            max_align = max(max_align, align)
        if not fields:
            return None
        return (_align_up(offset, max_align), max_align, fields)

    # ── ABI decisions ────────────────────────────────────────────────────
    def param_abi(self, ctype):
        """-> (StdrotType, type_name_or_None, pointer_level, reader) or None."""
        ctype = self.resolve(ctype)
        if ctype in SCALARS:
            stype = SCALARS[ctype][0]
            return (stype, None, 0, ("scalar", stype))
        if ctype == "const char *":
            return ("STDROT_CSTRING", None, 0, ("scalar", "STDROT_CSTRING"))
        if ctype in self.enums:
            return ("STDROT_INT", None, 0, ("scalar", "STDROT_INT"))
        if ctype in self.layouts:
            return ("STDROT_STRUCT", ctype, 0, ("struct", ctype))
        if ctype.endswith("*"):
            base = ctype[:-1].strip()
            if base.rstrip("*").strip() in self.callbacks:
                return None
            # STDROT_PTR itself is one level of indirection; pointer_level
            # counts the EXTRA levels beyond it (stdrot_api.h).
            level = ctype.count("*") - 1
            return ("STDROT_PTR", None, level, ("ptr", ctype))
        return None

    def return_abi(self, ctype):
        """-> (StdrotType, pointer_level, writer) or ('skip', reason)."""
        ctype = self.resolve(ctype)
        if ctype == "void":
            return ("STDROT_NONE", 0, ("void", None))
        if ctype in SCALARS:
            stype = SCALARS[ctype][0]
            return (stype, 0, ("scalar", stype))
        if ctype in self.enums:
            return ("STDROT_INT", 0, ("scalar", "STDROT_INT"))
        if ctype in self.structs:
            return ("skip", "struct return")
        if ctype == "const char *":
            return ("skip", "cstring return")
        if ctype.endswith("*"):
            return ("STDROT_PTR", ctype.count("*") - 1, ("ptr", ctype))
        return ("skip", "unsupported return type")


def _align_up(value, align):
    return (value + align - 1) // align * align


# ── Function selection ───────────────────────────────────────────────────
def build_functions(api, model, skips):
    """Decide, for every function in the API, whether it can be expressed
    and how. Returns the accepted ones in input order."""
    accepted = []
    by_brainrot_name = {}
    for fn in api.get("functions", []):
        name = fn["name"]
        params = fn.get("params") or []

        if any(p.get("type") == "..." or p.get("name") == "..."
               for p in params):
            skips.add("varargs", name)
            continue

        ret = model.return_abi(fn["returnType"])
        if ret[0] == "skip":
            skips.add(ret[1], name, model.resolve(fn["returnType"]))
            continue

        marshalled = []
        rejected = None
        for param in params:
            ctype = param["type"]
            base = model.resolve(ctype).rstrip("*").strip()
            if base in model.callbacks:
                rejected = ("callback param", base)
                break
            abi = model.param_abi(ctype)
            if abi is None:
                rejected = ("unsupported param type", model.resolve(ctype))
                break
            marshalled.append((param.get("name") or f"a{len(marshalled)}",
                               ctype, abi))
        if rejected:
            skips.add(rejected[0], name, rejected[1])
            continue

        # `void f(void)` is spelled as a single void param in the JSON, not
        # as an empty list -- it means zero arguments, not one.
        if len(marshalled) == 1 and model.resolve(marshalled[0][1]) == "void":
            marshalled = []

        brainrot_name = "rl_" + snake(name)
        clash = by_brainrot_name.get(brainrot_name)
        if clash:
            raise SystemExit(
                f"name collision: {name} and {clash} both map to "
                f"'{brainrot_name}'. snake() needs a special case, or these "
                "two need distinct Brainrot names -- letting both register "
                "would make validate_native_registry() reject the module at "
                "load time as a duplicate export.")
        by_brainrot_name[brainrot_name] = name

        accepted.append({
            "c_name": name,
            "brainrot_name": brainrot_name,
            "description": fn.get("description", ""),
            "params": marshalled,
            "ret": ret,
            "ret_ctype": model.resolve(fn["returnType"]),
        })
    return accepted


# ── Emitters ─────────────────────────────────────────────────────────────
GEN_BANNER_C = """\
/* GENERATED FILE -- DO NOT EDIT, DO NOT COMMIT.
 *
 * Produced by brainray/brainray_gen.py from {api_basename}
 * ({fn_count} of {fn_total} {library} functions, {struct_count} of
 * {struct_total} structs). Regenerate with `make brainray-gen`; edit the
 * generator, never this file.
 *
 * This is `lang.tab.c` by another name: derived, gitignored, and excluded
 * from `make format-check` (a generator that has to satisfy clang-format
 * is a generator nobody wants to change). See roadmap Appendix B Q7 for
 * why the generator and its pinned input are committed but this is not.
 */
"""


def emit_native(api, model, fns, opts, stats):
    """The C side: one adapter per function plus its StdrotEntry."""
    o = [GEN_BANNER_C.format(**stats)]
    o.append(f'#include "stdrot_api.h"')
    o.append(f"#include <{opts.header}>")
    o.append("#include <stddef.h>")
    o.append("#include <stdio.h>")
    o.append("#include <stdlib.h>")
    o.append("#include <string.h>")
    o.append("")
    o.append("""\
/* Reads a by-value aggregate argument out of its STDROT_STRUCT blob.
 *
 * The size check is the one STDROT_STRUCT's contract (stdrot_api.h) tells
 * a binding to make, and the reason it is a hard abort rather than a
 * clamp: the host promises the blob is that struct's C-ABI image, so a
 * mismatch means the host's computed layout and this compiler's layout
 * have diverged -- memcpy'ing anyway would read past the allocation and
 * hand a half-initialized struct to the library. The generated ABI-check
 * translation unit exists to make this unreachable at build time; this is
 * the runtime backstop for a host/module pair that were built separately.
 */
#define BR_READ_STRUCT(dst, arg, type)                                        \\
    do                                                                        \\
    {                                                                         \\
        if ((arg).val.blob.size != sizeof(type))                              \\
        {                                                                     \\
            br_struct_size_mismatch(#type, sizeof(type),                       \\
                                    (arg).val.blob.size);                     \\
        }                                                                     \\
        memcpy(&(dst), (arg).val.blob.data, sizeof(type));                    \\
    } while (0)

static void br_struct_size_mismatch(const char *type_name, size_t expected,
                                    size_t actual)
{
    fprintf(stderr,
            "brainray: struct '%s' is %zu bytes here but the interpreter "
            "passed %zu -- the binding and the interpreter disagree about "
            "layout; rebuild both from the same checkout\\n",
            type_name, expected, actual);
    abort();
}

/* LeakSanitizer: disclaiming the graphics stack's globals (issue #267).
 *
 * Same reasoning, and the same weak-symbol mechanism, as the hand-written
 * Road A module (brainray/raylib.c): the library and what it drives allocate
 * process-lifetime global state (a GL context, default font/shader, X11 and
 * font caches) that it never returns to the allocator, and the interpreter is
 * built with -fsanitize=address, so LSan reports all of it at exit.
 *
 * Every generated adapter brackets ITS LIBRARY CALL ONLY. Nothing the
 * interpreter allocates to make the call -- the argument vector, the
 * STDROT_STRUCT scratch copies, a STDROT_CSTRING buffer -- is inside the
 * bracket, because all of that lives in the host binary's
 * execute_native_call(), not here; so a real interpreter-side leak is still
 * caught. Road A brackets selectively (leaving pure getters out); this
 * brackets uniformly, because a generator cannot tell a polling getter from
 * an allocating one from the JSON, and suspending leak tracking across a call
 * that allocates nothing costs nothing.
 *
 * Weak symbols: these bind to libasan in a sanitizer build and are inert
 * no-ops otherwise (e.g. `make release`), so the module loads either way. */
__attribute__((weak)) void __lsan_disable(void);
__attribute__((weak)) void __lsan_enable(void);

static void br_lsan_ignore_begin(void)
{
    if (__lsan_disable)
    {
        __lsan_disable();
    }
}

static void br_lsan_ignore_end(void)
{
    if (__lsan_enable)
    {
        __lsan_enable();
    }
}
""")

    for fn in fns:
        o.append(_emit_one_adapter(fn, model))
    stats_note = (f"/* {len(fns)} adapters emitted. */")
    o.append(stats_note)
    o.append("")
    return "\n".join(o)


def _read_expr(abi, index):
    kind, payload = abi[3]
    if kind == "scalar":
        return f"args[{index}].val.{UNION_MEMBER[payload]}"
    if kind == "ptr":
        return f"({payload})args[{index}].val.ptr"
    return None  # struct: needs a statement, not an expression


def _emit_one_adapter(fn, model):
    c_name = fn["c_name"]
    lines = []
    if fn["description"]:
        lines.append(f"/* {fn['description']} */")
    lines.append(f"static StdrotValue br_{c_name}(StdrotValue *args, int argc)")
    lines.append("{")
    lines.append("    (void)argc;")
    if not fn["params"]:
        lines.append("    (void)args;")

    call_args = []
    for i, (pname, ctype, abi) in enumerate(fn["params"]):
        resolved = model.resolve(ctype)
        expr = _read_expr(abi, i)
        if expr is None:                      # by-value struct
            local = f"p{i}_{pname}"
            lines.append(f"    {resolved} {local};")
            lines.append(f"    BR_READ_STRUCT({local}, args[{i}], {resolved});")
            call_args.append(local)
        else:
            # Cast every scalar through its real C type: the ABI carries a
            # narrow set of tags (an `unsigned int` parameter arrives as
            # STDROT_INT), so the cast is what restores the library's own
            # declared type instead of relying on an implicit conversion
            # the compiler would warn about under -Wconversion.
            call_args.append(f"({resolved})({expr})")

    call = f"{c_name}({', '.join(call_args)})"
    ret_kind, ret_payload = fn["ret"][2]
    # The library call, and only the library call, runs with LSan tracking
    # suspended -- see the br_lsan_ignore_* comment in the emitted preamble.
    if ret_kind == "void":
        lines.append("    br_lsan_ignore_begin();")
        lines.append(f"    {call};")
        lines.append("    br_lsan_ignore_end();")
        lines.append("    return (StdrotValue){.type = STDROT_NONE};")
    elif ret_kind == "ptr":
        lines.append("    br_lsan_ignore_begin();")
        lines.append(f"    void *br_ret = (void *)({call});")
        lines.append("    br_lsan_ignore_end();")
        lines.append("    return (StdrotValue){.type = STDROT_PTR, "
                     ".val = {.ptr = br_ret}};")
    else:
        member = UNION_MEMBER[ret_payload]
        cast = {"STDROT_BOOL": "bool", "STDROT_CHAR": "char",
                "STDROT_SHORT": "short", "STDROT_INT": "int",
                "STDROT_FLOAT": "float", "STDROT_DOUBLE": "double"}[ret_payload]
        lines.append("    br_lsan_ignore_begin();")
        lines.append(f"    {cast} br_ret = ({cast})({call});")
        lines.append("    br_lsan_ignore_end();")
        lines.append(f"    return (StdrotValue){{.type = {ret_payload}, "
                     f".val = {{.{member} = br_ret}}}};")
    lines.append("}")

    # Descriptor
    if fn["params"]:
        lines.append(f"static const StdrotParam p_{c_name}[] = {{")
        for _, _, abi in fn["params"]:
            stype, tname, level, _ = abi
            name_lit = f'"{tname}"' if tname else "NULL"
            lines.append(f"    {{{stype}, {name_lit}, {level}}},")
        lines.append("};")
        params_ref = f"p_{c_name}"
    else:
        params_ref = "NULL"
    n = len(fn["params"])
    ret_type, ret_level = fn["ret"][0], fn["ret"][1]
    lines.append(f'STDROT_EXPORT_SIG("{fn["brainrot_name"]}", br_{c_name},')
    lines.append(f"                  ((StdrotParam){{{ret_type}, NULL, "
                 f"{ret_level}}}), {params_ref}, {n}, {n}, false);")
    lines.append("")
    return "\n".join(lines)


GEN_BANNER_BRAINROT = """\
🚽 GENERATED FILE -- DO NOT EDIT, DO NOT COMMIT.
🚽
🚽 Produced by brainray/brainray_gen.py from {api_basename}
🚽 ({struct_count} of {struct_total} {library} structs, {const_count}
🚽 constants). Regenerate with `make brainray-gen`.
🚽
🚽 This prelude is the types-and-constants half of the binding. It exists
🚽 because `StdrotAPI` carries a function table and nothing else -- there is
🚽 no ABI channel for registering a type or a constant, and Phase 4 left
🚽 that deliberately undesigned. It doesn't need one: a `#cooked <name>`
🚽 resolves a "<name>.brainrot" prelude BEFORE a "<name>.so", and a prelude
🚽 may itself #cooked a native module, so types and constants ship as
🚽 ordinary Brainrot source and only the functions go through the C ABI.
🚽
🚽 Every `gang` below is laid out to match the real C struct byte for byte;
🚽 {abi_check_basename} asserts that against the actual {library} headers at
🚽 compile time, and the sizes in each comment are what this generator
🚽 computed.
"""


def emit_prelude(model, consts, opts, stats):
    o = [GEN_BANNER_BRAINROT.format(**stats)]
    o.append(f"#cooked <{opts.module_name}_native>")
    o.append("")

    o.append("🚽 ── Types ─────────────────────────────────────────────────")
    for name in model.emittable:
        size, align, fields = model.layouts[name]
        o.append(f"🚽 {name}: {size} bytes, align {align}")
        o.append(f"gang {name} {{")
        for fname, offset, ctype in fields:
            if ctype in SCALARS:
                keyword = SCALARS[ctype][1]
                spelling = f"{keyword} {fname};"
            else:
                spelling = f"gang {ctype} {fname};"
            o.append(f"    {spelling:<28}🚽 offset {offset} ({ctype})")
        o.append("};")
        o.append("")

    if consts:
        o.append("🚽 ── Constants ─────────────────────────────────────────────")
        o.append("🚽 Emitted as `gyatt` enums; Brainrot enum constants are")
        o.append("🚽 globally visible, so these are usable bare (KEY_SPACE).")
        for enum_name, values in consts:
            o.append(f"gyatt {enum_name} {{")
            for i, (cname, cvalue) in enumerate(values):
                comma = "," if i + 1 < len(values) else ""
                o.append(f"    {cname} = {cvalue}{comma}")
            o.append("};")
            o.append("")
    return "\n".join(o)


GEN_BANNER_ABI = """\
/* GENERATED FILE -- DO NOT EDIT, DO NOT COMMIT.
 *
 * Produced by brainray/brainray_gen.py from {api_basename}.
 *
 * ABI drift detection, and the reason this binding is allowed to memcpy
 * bytes into real C types at all. Every assertion below compares a REAL
 * {library} struct, as this compiler lays it out from the actual headers,
 * against the size/offset numbers the generator independently computed
 * from {api_basename} -- the same numbers it emitted as `gang` field
 * offsets in the Brainrot prelude.
 *
 * So a failure here means one of exactly three things, all of which are
 * bugs worth stopping the build for:
 *   1. the pinned {api_basename} no longer describes the installed
 *      {library} (someone upgraded one without the other);
 *   2. this platform's C ABI differs from the generator's layout model
 *      (its SCALARS table, or its padding rules);
 *   3. {library} changed a struct.
 *
 * This is a translation unit, not a runnable test: it asserts at compile
 * time and its main() only prints, so building it IS the check.
 */
"""


def emit_abi_check(model, opts, stats):
    o = [GEN_BANNER_ABI.format(**stats)]
    o.append(f"#include <{opts.header}>")
    o.append("#include <stddef.h>")
    o.append("#include <stdio.h>")
    o.append("")
    for name in model.emittable:
        size, align, fields = model.layouts[name]
        o.append(f"_Static_assert(sizeof({name}) == {size},")
        o.append(f'               "{name}: real size disagrees with the '
                 f'{size} bytes brainray-gen computed");')
        o.append(f"_Static_assert(_Alignof({name}) == {align},")
        o.append(f'               "{name}: real alignment disagrees with the '
                 f'{align} brainray-gen computed");')
        for fname, offset, _ in fields:
            o.append(f"_Static_assert(offsetof({name}, {fname}) == {offset},")
            o.append(f'               "{name}.{fname}: real offset disagrees '
                     f'with the {offset} brainray-gen computed");')
        o.append("")
    o.append("int main(void)")
    o.append("{")
    o.append(f'    printf("brainray ABI check: {len(model.emittable)} '
             f'{opts.library} structs verified against real headers\\n");')
    for name in model.emittable:
        o.append(f'    printf("  %-18s sizeof=%2zu _Alignof=%zu\\n", "{name}", '
                 f"sizeof({name}), _Alignof({name}));")
    o.append("    return 0;")
    o.append("}")
    o.append("")
    return "\n".join(o)


def build_constants(model, api, skips):
    """Collect enum constants for the prelude.

    Brainrot enum constants live in ONE global namespace
    (find_global_enum_constant(), ast.c), unlike C where they are at least
    scoped to a translation unit -- so a name appearing in two enums would
    be a redeclaration, not a shadow. Deduplicated here, keeping the first
    occurrence, because emitting both would make the generated prelude fail
    to parse and the failure would point at the prelude rather than at the
    duplicate that caused it.
    """
    out = []
    seen = {}
    for enum in api.get("enums", []):
        values = []
        for value in enum.get("values", []):
            name = value["name"]
            if name in seen:
                skips.add("duplicate constant", f"{enum['name']}.{name}",
                          f"already in {seen[name]}")
                continue
            seen[name] = enum["name"]
            values.append((name, value["value"]))
        if values:
            out.append((enum["name"], values))
    return out


def report(fns, model, api, skips, consts, repaired, opts, out):
    total_fns = len(api.get("functions", []))
    total_structs = len(model.structs)
    pct = 100.0 * len(fns) / total_fns if total_fns else 0.0
    p = out.append
    p(f"brainray-gen: {opts.library} <- {opts.api}")
    if repaired:
        p(f"  NOTE: repaired {repaired} malformed `description` value(s) "
          "while parsing (upstream JSON defect; see load_api())")
    p(f"  functions : {len(fns)}/{total_fns} emitted ({pct:.0f}%)")
    p(f"  structs   : {len(model.emittable)}/{total_structs} emitted")
    p(f"  constants : {sum(len(v) for _, v in consts)} in {len(consts)} enums")
    if skips.counts:
        p("  skipped:")
        for reason, count in skips.counts.most_common():
            sample = ", ".join(skips.examples[reason][:3])
            more = " ..." if skips.counts[reason] > 3 else ""
            p(f"    {count:4}  {reason:<26} e.g. {sample}{more}")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Generate a Brainrot native binding from a C library's "
                    "machine-readable API description.")
    ap.add_argument("--api", required=True,
                    help="vendored, pinned API description (JSON)")
    ap.add_argument("--outdir", required=True,
                    help="directory for generated files (created if absent)")
    ap.add_argument("--header", default="raylib.h",
                    help="library header the adapters #include (default: "
                         "raylib.h)")
    ap.add_argument("--module-name", default="raylibgen",
                    help="Brainrot module name: emits <name>.brainrot and "
                         "<name>_native.c (default: raylibgen)")
    ap.add_argument("--library", default="raylib",
                    help="human-readable library name for comments/reports")
    ap.add_argument("--strict", action="store_true",
                    help="fail if any skip reason is not one of the known, "
                         "documented ABI gaps -- this is what makes an "
                         "upstream schema change a build failure instead of "
                         "a quietly smaller binding")
    opts = ap.parse_args(argv)

    import os
    api, repaired = load_api(opts.api)
    model = TypeModel(api)
    unbuildable = model.compute_layouts()

    skips = Skips()
    for name in sorted(unbuildable):
        skips.add("unsupported struct field", name)
    fns = build_functions(api, model, skips)
    consts = build_constants(model, api, skips)

    os.makedirs(opts.outdir, exist_ok=True)
    native_path = os.path.join(opts.outdir, f"{opts.module_name}_native.c")
    prelude_path = os.path.join(opts.outdir, f"{opts.module_name}.brainrot")
    abi_path = os.path.join(opts.outdir, f"{opts.module_name}_abi_check.c")

    stats = {
        "api_basename": os.path.basename(opts.api),
        "abi_check_basename": os.path.basename(abi_path),
        "library": opts.library,
        "fn_count": len(fns),
        "fn_total": len(api.get("functions", [])),
        "struct_count": len(model.emittable),
        "struct_total": len(model.structs),
        "const_count": sum(len(v) for _, v in consts),
    }

    with open(native_path, "w", encoding="utf-8") as fh:
        fh.write(emit_native(api, model, fns, opts, stats))
    with open(prelude_path, "w", encoding="utf-8") as fh:
        fh.write(emit_prelude(model, consts, opts, stats))
    with open(abi_path, "w", encoding="utf-8") as fh:
        fh.write(emit_abi_check(model, opts, stats))

    lines = []
    report(fns, model, api, skips, consts, repaired, opts, lines)
    lines.append(f"  wrote {native_path}")
    lines.append(f"  wrote {prelude_path}")
    lines.append(f"  wrote {abi_path}")
    print("\n".join(lines))

    unexpected = skips.unexpected()
    if unexpected:
        msg = ("brainray-gen: UNEXPECTED skip reason(s): "
               + ", ".join(unexpected)
               + "\n  These are not known ABI gaps -- the upstream API "
                 "description probably changed shape. Investigate before "
                 "trusting this binding.")
        if opts.strict:
            raise SystemExit(msg)
        print(msg, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
