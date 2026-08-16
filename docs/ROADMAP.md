# Brainrot Roadmap

> Status: living document. Baseline commit: `0cfa958` (main).
> Everything under "Where we are today" was verified against the source at that
> commit; everything else is a plan and is negotiable.

Brainrot started as a meme keyword substitution over C. It has since grown a real
pipeline (Flex → Bison → AST → semantic analyzer → tree-walking interpreter), a
`dlopen`-based standard library (`libstdrot.so`), pointers, and structs (`gang`).

This roadmap describes how it becomes a cursed-but-genuinely-useful language:
one that can call native C libraries, run a game loop, spawn threads, hold a
hashmap, and serve HTTP — without any of those being one-off hacks bolted onto
the interpreter.

**The organizing idea:** we do not port libraries into Brainrot. We build a good
enough native ABI that every C library — raylib, SQLite, libcurl — becomes just
another binding, most of it generated.

---

## Table of Contents

1. [Where we are today](#1-where-we-are-today)
2. [Guiding principles](#2-guiding-principles)
3. [Phase 1 — The keystone: native calls as expressions](#phase-1--the-keystone-native-calls-as-expressions)
4. [Phase 2 — A typed native ABI](#phase-2--a-typed-native-abi)
5. [Phase 3 — C-compatible aggregates](#phase-3--c-compatible-aggregates)
6. [Phase 4 — Native modules and `#cooked`](#phase-4--native-modules-and-cooked)
7. [Phase 5 — Bindings and the first cursed game](#phase-5--bindings-and-the-first-cursed-game)
8. [Phase 6 — Threads (`yeet`)](#phase-6--threads-yeet)
9. [Phase 7 — Hashmaps (`grindset`)](#phase-7--hashmaps-grindset)
10. [Phase 8 — Sockets and web servers](#phase-8--sockets-and-web-servers)
11. [Milestones](#milestones)
12. [Appendix A — Reserved keywords](#appendix-a--reserved-keywords)
13. [Appendix B — Open questions](#appendix-b--open-questions)

---

## 1. Where we are today

We already built the beginning of an FFI system, somewhat by accident.
`libstdrot.so` is loaded with `dlopen`/`dlsym`, native functions self-register
through a linker section (`STDROT_EXPORT`), and every call is dispatched through
one generic signature:

```c
typedef StdrotValue (*StdrotFn)(StdrotValue *args, int argc);
```

That is a real foreign-function substrate in embryo. What is missing is
everything that makes it *typed* and *composable*.

### Verified limitations

| # | Limitation | Evidence |
| - | ---------- | -------- |
| L1 | Native calls are statement-only. In expression position they route to `execute_function_call()`, which only knows user-defined functions, and fail with `Undefined function`. | [ast.c:3588](../ast.c#L3588), [ast.c:2414](../ast.c#L2414) |
| L2 | Return values are discarded. The only way a builtin produces a value is a write-back hack: if argument #1 is an identifier, the result is stored into that variable. | [stdrot.c:319](../stdrot.c#L319) |
| L3 | The semantic analyzer types every builtin as `NONE` — it knows a name exists and nothing more. | [semantic_analyzer.c:352](../semantic_analyzer.c#L352) |
| L4 | The registry carries no signature: `StdrotEntry` is `{ name, fn }`. | [stdrot/stdrot_api.h:86](../stdrot/stdrot_api.h#L86) |
| L5 | `StdrotValue` has no pointer, struct, or handle representation — only `int/float/double/short/bool/char/String/void`. | [stdrot/stdrot_api.h:49](../stdrot/stdrot_api.h#L49) |
| L6 | `compute_struct_layout()` packs fields sequentially with no alignment or padding, so `gang` layouts do not match C. | [ast.c:3948](../ast.c#L3948) |
| L7 | Struct member access only resolves when the base is a plain identifier; `a.b.c` is rejected outright. | [ast.c:346](../ast.c#L346) |
| L8 | Functions cannot return structs. | [ast.c:2458](../ast.c#L2458) |
| L9 | `StructField` keeps `VarType + pointer_level + offset` only — no struct type name, no arrays, no modifiers, so nested-struct and fixed-array fields are unrepresentable. | [ast.h:82](../ast.h#L82) |
| L10 | Exactly one native library is ever loaded, hardcoded as `libstdrot.so`. | [Makefile:52](../Makefile#L52) |

Reproducing L1/L2 takes four lines:

```c
skibidi main {
    cap ok = bet(W);   /* Error: Undefined function */
    bussin 0;
}
```

`bet` *is* registered and *does* return a value; it simply cannot be used as one.

### What already works in our favour

- Brainrot has pointers, pointer levels, address-of and dereference.
- `gang` struct definitions, instances, and single-level member access work.
- The registry is discovery-based — the host hardcodes no function names.
- Arena allocation, a hashmap (`lib/hm.c`), and `String` are already in-tree.
- The build is `-Werror` with ASan+UBSan, and `make valgrind` covers every test
  case. That's an unusually strong safety net for the work below, which is
  almost entirely memory-layout work.

---

## 2. Guiding principles

1. **Don't port, bind.** raylib is already a C library. Our job is an ABI.
2. **Generate, don't hand-write.** Hundreds of wrappers written by hand is how
   this project dies. Generated C adapters give compile-time-checked calls.
3. **No libffi.** Generated C means the C compiler does the ABI work for us. We
   are not writing a dynamic call machine.
4. **Layout correctness is testable — so test it.** Every ABI claim gets a
   `_Static_assert` or a runtime `offsetof` check against the real C header.
5. **Meme integrity is a hard requirement, not a joke.** New keywords must be
   funny *and* mnemonic. `yeet` for spawning a thread is both.
6. **Nothing merges without `make test` and `make valgrind`.** Sanitizer output
   is a build failure, not a suggestion.

---

## Phase 1 — The keystone: native calls as expressions

**Status: not started · Priority: P0 · Blocks: literally everything else**

This is the single highest-leverage change in the entire roadmap. Until a native
call is an ordinary expression, none of the rest is worth starting.

### Goal

`NODE_FUNC_CALL` must not care whether the callee is Brainrot or native.

```c
goon (WindowShouldClose() == L) { ... }   /* today: parse-time OK, runtime error */
rizz n = strlen_native(s);                /* today: impossible */
```

### Work

1. Add `StdrotValue execute_native_call(const String name, ArgumentList *args)`
   in `stdrot.c` that returns the result instead of swallowing it.
2. In `handle_function_call()` ([ast.c:2414](../ast.c#L2414)), check the native
   registry *before* failing with `Undefined function`, and marshal the returned
   `StdrotValue` into `current_return_value`.
3. Delete the argument write-back hack ([stdrot.c:319](../stdrot.c#L319)) once
   `slorp`'s callers are migrated to `rizz x = slorp(...)` form.
4. Keep statement-position calls working — a discarded return value is fine.

### Compatibility note

Removing the write-back changes the meaning of existing `slorp(x);` programs.
That is a **breaking change to a public surface** and needs sign-off before it
lands. Suggested path: support both forms for one release, warn on the old one.

### Definition of done

- `test_cases/native_call_expr.brainrot` covering a native call in an
  initializer, a condition, an argument, and a binary operand.
- `bet(W)` usable as a `cap` value.
- `make test` and `make valgrind` green.

---

## Phase 2 — A typed native ABI

**Status: not started · Priority: P0 · Depends on: Phase 1**

Right now the semantic analyzer cannot check a single builtin argument. Making
native exports self-describing fixes that and unlocks generated bindings.

### Extend `StdrotType`

```c
typedef enum {
    STDROT_VOID,
    STDROT_I8,  STDROT_U8,
    STDROT_I16, STDROT_U16,
    STDROT_I32, STDROT_U32,
    STDROT_I64, STDROT_U64,
    STDROT_F32, STDROT_F64,
    STDROT_BOOL,
    STDROT_STRING,    /* Brainrot String  */
    STDROT_CSTRING,   /* NUL-terminated   */
    STDROT_PTR,
    STDROT_STRUCT,
    STDROT_HANDLE,
} StdrotType;
```

Not every width is needed on day one. `STDROT_PTR` and honest return values are
mandatory; the rest can land incrementally.

### Make the registry descriptive

```c
typedef struct {
    StdrotType  type;
    const char *type_name;      /* for STDROT_STRUCT / STDROT_HANDLE */
    int         pointer_level;
} StdrotParam;

typedef struct {
    const char        *name;
    StdrotParam        return_type;
    const StdrotParam *params;
    int                param_count;
    StdrotFn           fn;
} StdrotEntry;
```

Then the analyzer knows `WindowShouldClose() -> bool`, `GetFrameTime() -> float`,
`LoadTexture(const char *) -> Texture2D`, and can type-check native calls exactly
the way it type-checks Brainrot-defined functions — replacing the `return NONE`
at [semantic_analyzer.c:352](../semantic_analyzer.c#L352).

### The string boundary

Brainrot uses `String { char *data; size_t len; }`; C libraries want
`const char *`. Define the conversion **once**, in the ABI layer, rather than
scattering `malloc(len+1)/memcpy/'\0'` through every wrapper. Ownership rule to
decide up front: adapter-owned scratch buffer, freed after the call, unless the
signature is annotated as escaping.

### Definition of done

- `STDROT_EXPORT_SIG(...)` macro alongside the legacy `STDROT_EXPORT`.
- Existing builtins migrated to signatures.
- Semantic errors for arity and type mismatches on native calls, with tests.

---

## Phase 3 — C-compatible aggregates

**Status: not started · Priority: P0 · Depends on: Phase 2**

This is the big one. raylib is struct city: `Vector2`, `Vector3`, `Color`,
`Rectangle`, `Texture2D`, `Image`, `Camera2D`, `Camera3D`, `Matrix`, and further
down `Mesh`, `Shader`, `Model`, `Sound`, `Music`.

### 3a. Fix the layout (do this first — it is a live correctness bug)

[ast.c:3948](../ast.c#L3948) packs fields with no padding. Any `gang` handed to C
today would be silently misinterpreted. Required:

```c
offset      = align_up(offset, alignof(field));
field->offset = offset;
offset     += sizeof(field);
struct_size = align_up(offset, max_field_alignment);
```

And prove it, don't assume it:

```c
_Static_assert(sizeof(BrainrotVector2) == sizeof(Vector2), "ABI drift");
_Static_assert(offsetof(BrainrotVector2, y) == offsetof(Vector2, y), "ABI drift");
```

### 3b. Introduce a real type descriptor

`VarType + pointer_level + modifiers` is at the end of its rope. Replace it with:

```c
typedef struct TypeDesc {
    TypeKind    kind;
    bool        is_unsigned;
    int         pointer_level;
    StructDef  *struct_def;      /* kind == TYPE_STRUCT */
    int         array_rank;
    size_t      dimensions[MAX_DIMENSIONS];
} TypeDesc;
```

This is invasive — it touches the AST, the analyzer, and the interpreter — but
every remaining item on this roadmap gets cheaper afterwards, and the alternative
is stretching `VarType` until it snaps.

### 3c. `gang` as a first-class type

Struct fields, parameters, and return types currently come from the primitive
`type` grammar, so a struct-typed field cannot even be spelled. Target:

```c
gang Vector2 { chad x; chad y; };

gang Camera2D {
    gang Vector2 offset;
    gang Vector2 target;
    chad rotation;
    chad zoom;
};

gang Vector2 GetMousePosition();
skibidi DrawCircleV(gang Vector2 center, chad radius, gang Color color);
```

Requires struct-typed fields/params/returns, struct arguments and returns
(removing [ast.c:2458](../ast.c#L2458)), and struct assignment semantics
(decision needed: value copy, matching C).

### 3d. Nested member access

Rewrite member access as `base lvalue address + field offset`, recursively,
instead of special-casing `NODE_IDENTIFIER` at [ast.c:346](../ast.c#L346). Once
access is address-based, `camera.target.x`, `model.transform.m0`, and
`renderTexture.texture.width` all fall out for free — including as assignment
targets.

### 3e. Remaining C field types

For full raylib coverage: unsigned and fixed-width scalars, pointer fields,
nested struct fields, fixed arrays inside structs (`char name[32]`,
`float params[4]`), struct aliases, and arrays/pointers to structs.

`gyatt` (enum), `lit` (typedef), and `chungus` (union) are **not** blockers — a
generator can emit `CAMERA_PERSPECTIVE`, `KEY_SPACE`, and `MOUSE_BUTTON_LEFT` as
ordinary Brainrot integer constants. They stay on the wishlist.

---

## Phase 4 — Native modules and `#cooked`

**Status: not started · Priority: P1 · Depends on: Phase 2**

Today exactly one `.so` is loaded, by hardcoded name. Generalize to a module
directory, each exporting a discovery entrypoint:

```
stdrot/     brainray/     brainsql/     braincurl/
```

```c
StdrotAPI brainrot_module_init(void);
```

Then `#cooked <raylib>`, currently listed as unimplemented in the README, means:
locate module → `dlopen` → fetch metadata → register types, constants, and
functions. That is a far better fate for the directive than textual inclusion.

---

## Phase 5 — Bindings and the first cursed game

**Status: not started · Priority: P1**

There are two roads here and we should walk both, in order.

### Road A — maximum brainrot, immediately (needs only Phase 1)

Link raylib into `libstdrot.so` and hand-write ~20 wrappers over primitives
only. Textures become integer handles; C owns the `Texture2D textures[]` array
and Brainrot holds an ID.

```c
skibidi main {
    rl_init_window(1280, 720, "Ohio Engine");

    goon (rl_window_should_close() == L) {
        rl_begin_drawing();
        rl_clear_background(20, 20, 20, 255);
        rl_draw_circle(640, 360, 100.0, 255, 0, 255, 255);
        rl_draw_text("ABSOLUTE CINEMA", 500, 500, 32, 255, 255, 255, 255);
        rl_end_drawing();
    }

    rl_close_window();
}
```

The joke language runs a game loop. Ships as `examples/ohio_engine.brainrot`.

### Road B — generate the real binding (needs Phases 2–4)

raylib ships `tools/rlparser/output/raylib_api.json`, a machine-readable
description of its entire API. Point a generator at it:

```
raylib_api.json → brainray-gen → { C adapters, native descriptors,
                                   Brainrot constants/types, docs, ABI tests }
```

```c
static StdrotValue br_LoadTexture(StdrotValue *args, int argc)
{
    Texture2D tex = LoadTexture(to_cstr(args[0]));
    return stdrot_struct("Texture2D", &tex, sizeof(tex));
}
```

Generated C is compile-time correct and vastly easier to reason about than a
dynamic call machine. Once this works, raylib is merely the first client: SDL,
SQLite, libcurl, OpenSSL, PortAudio, Lua, FFmpeg, and libgit2 are the same
problem.

---

## Phase 6 — Threads (`yeet`)

**Status: not started · Priority: P1 · Depends on: Phase 1**

pthreads, behind slang. `yeet` launches work into the void; you get it `caught in
4k` later.

| Brainrot | C equivalent | Mnemonic |
| -------- | ------------ | -------- |
| `yeet` | `pthread_create` | launch it into the void |
| `caught in 4k` | `pthread_join` | catch what you yeeted, on camera |
| `gatekeep` | mutex type / `pthread_mutex_lock` | nobody else gets in |
| `letcook` | `pthread_mutex_unlock` | let the next one cook |
| `bed rotting` | `pthread_cond_wait` | blocked, doing nothing, in bed |
| `pick me` | `pthread_cond_signal` | wakes exactly one waiter |

```c
skibidi worker(rizz id) {
    gatekeep(counter_lock);
    counter = counter + 1;
    letcook(counter_lock);
}

skibidi main {
    yeet t1 = yeet worker(1);
    yeet t2 = yeet worker(2);
    caught in 4k t1;
    caught in 4k t2;
    bussin 0;
}
```

`pick me` wakes one waiter, matching `pthread_cond_signal`. If we also want
`pthread_cond_broadcast`, `ratioed` is the obvious name — everyone piles on at
once.

Multi-word keywords are already precedent: `"sigma rule"` lexes as `case` at
[lang.l:72](../lang.l#L72), so `caught in 4k`, `bed rotting`, and `pick me` need
no new lexer machinery. The digits in `4k` are harmless inside a quoted Flex
pattern — longest-match means it never competes with number literals.

### The hard part is not pthreads

It is that the interpreter is built around process-global state: `current_scope`,
`current_return_value`, the `setjmp` jump-buffer stack, and the arena allocator
are all shared and none are thread-safe. Options, in increasing order of effort:

- **A. Green threads / cooperative scheduling.** No data races by construction,
  no real parallelism. Cheapest, and honestly quite funny.
- **B. Real pthreads + a global interpreter lock.** Real threads, real blocking
  I/O overlap, no parallel compute. The Python road.
- **C. Thread-local interpreter state.** Actual parallelism. Requires making
  scope, return value, and jump buffers thread-local and the arena either
  per-thread or locked.

**Recommendation: B first, C as a follow-up.** A GIL gets `yeet` shipping and
correct; the interpreter-state refactor is a large independent project and
should not block the feature landing.

Thread-safety of `libstdrot.so` (notably `g_exec_context`, which is a global)
must be settled in the same change.

---

## Phase 7 — Hashmaps (`grindset`)

**Status: not started · Priority: P1**

The grindset: you put things in, you get things out, it never stops. `lib/hm.c`
already exists internally and is a plausible starting point, though it will need
generic key/value support.

| Operation | Keyword | Mnemonic |
| --------- | ------- | -------- |
| declare | `grindset` | it never stops |
| insert | `ship` | shipping = pairing two things (key + value) |
| lookup | `clock it` | to clock something is to spot it |
| membership | `sus` | you suspect it's in there |
| delete | `unalive` | self-explanatory |
| size | `bagsize` | how big is the bag |
| iterate | `flex` over a `grindset` | reuses the existing loop keyword |

```c
skibidi main {
    grindset<rant, rizz> scores;

    ship(scores, "ohio", 100);
    ship(scores, "skibidi", 42);

    edgy (sus(scores, "ohio")) {
        yapping("ohio: %d", clock it(scores, "ohio"));
    }

    yapping("entries: %d", bagsize(scores));
    bussin 0;
}
```

`clock it(map, key)` reads awkwardly in call position — a fair argument that
lookup should be a plain library function named `clock_it`, or that the whole
map API should be. See the note at the end of Appendix A.

### Design decisions needed

- **Generics.** `grindset<K, V>` implies a type parameter system Brainrot does
  not have. Alternative: a dynamically-typed map keyed by `rant` only, which is
  far cheaper and covers most real use. **Recommendation: start string-keyed and
  monomorphic**, generalize later.
- **Memory.** Maps outlive statements, so they need clear ownership. Arena is a
  poor fit for a growing structure; likely needs explicit lifetime tied to scope
  exit.
- **This is the first heap-managed aggregate in the language**, so it sets
  precedent for lists/dynamic arrays later. Worth designing carefully.

---

## Phase 8 — Sockets and web servers

**Status: not started · Priority: P2 · Depends on: Phases 1, 6**

Linux `socket.h` wrapped in slang. Keywords below are **proposals** and need
sign-off before they touch `lang.l`.

| Brainrot | C equivalent | Rationale |
| -------- | ------------ | --------- |
| `drip` | `socket()` | the conduit data flows through |
| `soft launch` | `bind()` | quietly announcing yourself at an address |
| `lurk` | `listen()` | lurking for connections |
| `snatched` | `accept()` | you snatch the incoming client |
| `stan` | `connect()` | you stan a remote server |
| `dm` | `send()` / `write()` | self-explanatory |
| `peep` | `recv()` / `read()` | peep the message |
| `ghost` | `close()` | ghosting the connection |

```c
skibidi main {
    rizz server = drip();
    soft launch(server, 8080);
    lurk(server, 128);

    goon (W) {
        rizz client = snatched(server);
        yeet handle(client);
    }
}
```

### Layering

1. **L1 — raw sockets.** Thin wrappers, blocking, one client at a time.
2. **L2 — concurrency.** `yeet` per connection (Phase 6) or an `epoll` loop.
3. **L3 — HTTP.** A minimal request parser and router in `brainhttp`, built on
   L1/L2, not on new keywords:

```c
skibidi on_request(gang Request req, gang Response res) {
    edgy (req.path == "/") {
        res.status = 200;
        dm(res, "gm");
    }
}
```

L3 wants `gang` structs with string fields, so it lands after Phase 3.
Note that everything here is ordinary C library surface — sockets need almost no
new *language* features, which is why this is P2 rather than P0. It is mostly a
consumer of the FFI work.

---

## Milestones

| Milestone | Contents | Unlocks |
| --------- | -------- | ------- |
| **M1 — Values flow** | Phase 1 | Native calls in expressions. Road A raylib demo. |
| **M2 — Types flow** | Phase 2 | Type-checked native calls; pointers/handles in the ABI. |
| **M3 — Layout is honest** | Phase 3a + 3d | ABI-correct `gang`; `a.b.c` works. |
| **M4 — Aggregates are first-class** | Phase 3b, 3c, 3e | `gang Vector2 GetMousePosition()`. |
| **M5 — Modules** | Phase 4 | `#cooked <raylib>` means something. |
| **M6 — Generated bindings** | Phase 5 Road B | raylib as first client; other libraries follow. |
| **M7 — Concurrency** | Phase 6 | `yeet` / `caught in 4k`. |
| **M8 — Data structures** | Phase 7 | `grindset`. |
| **M9 — Network** | Phase 8 | Brainrot serves HTTP. |

M1–M4 are strictly ordered. M7 and M8 are independent of M2–M6 and can proceed
in parallel by anyone who wants them.

### If you only do one thing

Make `StdrotFn` results flow through the ordinary expression and type system.
Once `rizz x = native_func();` works and the analyzer knows
`native_func() -> rizz`, everything above becomes incremental. Then fix
C-compatible `gang` layout. Then point a generator at `raylib_api.json`. Then
commit the first game as something appropriately cursed.

---

## Appendix A — Reserved keywords

Proposed additions. Nothing here conflicts with a keyword currently in `lang.l`.

| Keyword | Meaning | Phase | Status |
| ------- | ------- | ----- | ------ |
| `yeet` | spawn thread | 6 | proposed |
| `caught in 4k` | join thread | 6 | proposed |
| `gatekeep` | mutex / lock | 6 | proposed |
| `letcook` | unlock | 6 | proposed |
| `bed rotting` | condition wait | 6 | proposed |
| `pick me` | condition signal | 6 | proposed |
| `ratioed` | condition broadcast | 6 | optional |
| `grindset` | hashmap | 7 | proposed |
| `ship` | map insert | 7 | proposed |
| `clock it` | map lookup | 7 | proposed |
| `sus` | map contains | 7 | proposed |
| `unalive` | map delete | 7 | proposed |
| `bagsize` | map size | 7 | proposed |
| `drip` | socket | 8 | proposed |
| `soft launch` | bind | 8 | proposed |
| `lurk` | listen | 8 | proposed |
| `snatched` | accept | 8 | proposed |
| `stan` | connect | 8 | proposed |
| `dm` | send | 8 | proposed |
| `peep` | recv | 8 | proposed |
| `ghost` | close | 8 | proposed |

None of these collide with a keyword currently in `lang.l`, with a registered
builtin (`yapping`, `yappin`, `baka`, `bet`, `chill`, `ragequit`, `slorp`), or
with a proposed preprocessor directive. `letcook` is deliberately one word so it
cannot be confused with the `#cooked` directive.

Per `AGENTS.md`, the README keyword table is a public compatibility surface.
These are additive, but the table must be updated in the same PR that implements
each keyword, and any change to an *existing* keyword needs explicit sign-off.

Several of these could reasonably be library functions instead of keywords —
`ship`/`clock it`/`dm`/`peep` in particular. Keyword status buys syntax; it costs
grammar complexity and a reserved identifier forever. Decide per keyword, and
default to "library function" when in doubt.

The multi-word names are the sharpest version of this trade. `caught in 4k t1;`
reads beautifully as a statement, because that is a statement-shaped operation.
`clock it(scores, "ohio")` reads badly, because lookup is expression-shaped and
a phrase does not sit well in call position. A reasonable rule: **phrases earn
keyword status only where they appear in statement position**; everything
expression-shaped becomes a snake_case library function (`clock_it`, `soft_launch`)
and keeps the joke without fighting the grammar.

---

## Appendix B — Open questions

1. **Write-back removal (Phase 1).** Does `slorp(x);` keep working? Deprecation
   window, or clean break?
2. **`TypeDesc` migration (Phase 3b).** One large refactor, or incremental with
   both representations alive? The latter is safer and uglier.
3. **Struct assignment.** Value copy (C semantics) or reference? Affects
   everything downstream.
4. **Threading model (Phase 6).** Green threads, GIL, or thread-local state?
   Recommendation above is GIL-first, but this is a real fork in the road.
5. **`grindset` generics (Phase 7).** String-keyed monomorphic, or a real type
   parameter system? The latter is a language-design project of its own.
6. **Ownership of native resources.** Textures, sockets, and map entries all
   outlive statements. Brainrot has no destructors and no GC. Handles sidestep
   this by keeping ownership in C — is that the general answer?
7. **Generated code in-tree or built?** `raylib_api.json` output could be
   committed or generated at build time. `AGENTS.md` forbids committing
   generated files; does that rule extend to bindings, and if so, does raylib
   become a build dependency?
