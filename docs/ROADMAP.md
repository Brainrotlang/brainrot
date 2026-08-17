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
11. [Phase 9 — Unit testing (`sussybaka`)](#phase-9--unit-testing-sussybaka)
12. [Phase 10 — File I/O](#phase-10--file-io)
13. [Milestones](#milestones)
14. [Appendix A — Reserved keywords](#appendix-a--reserved-keywords)
15. [Appendix B — Open questions](#appendix-b--open-questions)

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

pthreads, behind slang. `yeet` launches work into the void; you `mogged` it back
when you need the result.

| Brainrot | C equivalent | Mnemonic |
| -------- | ------------ | -------- |
| `yeet` | `pthread_create` | launch it into the void |
| `mogged` | `pthread_join` | main mogs the worker back into itself |
| `gatekeep` | mutex type / `pthread_mutex_lock` | nobody else gets in |
| `letcook` | `pthread_mutex_unlock` | let the next one cook |
| `simp` | `pthread_cond_wait` | waiting around for someone to notice you |
| `yass` | `pthread_cond_signal` | hypes exactly one waiter awake |

```c
skibidi worker(rizz id) {
    gatekeep(counter_lock);
    counter = counter + 1;
    letcook(counter_lock);
}

skibidi main {
    yeet t1 = yeet worker(1);
    yeet t2 = yeet worker(2);
    mogged t1;
    mogged t2;
    bussin 0;
}
```

`yass` wakes one waiter, matching `pthread_cond_signal`. If we also want
`pthread_cond_broadcast`, `ratioed` is the obvious counterpart — everyone piles
on at once.

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
| lookup | `blorbo` | go get your favourite little guy by name |
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
        yapping("ohio: %d", blorbo(scores, "ohio"));
    }

    yapping("entries: %d", bagsize(scores));
    bussin 0;
}
```

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
| `brat` | `bind()` | confidently claiming your spot |
| `lurk` | `listen()` | lurking for connections |
| `snatched` | `accept()` | you snatch the incoming client |
| `stan` | `connect()` | you stan a remote server |
| `dm` | `send()` / `write()` | self-explanatory |
| `peep` | `recv()` / `read()` | peep the message |
| `ghost` | `close()` | ghosting the connection |

```c
skibidi main {
    rizz server = drip();
    brat(server, 8080);
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

## Phase 9 — Unit testing (`sussybaka`)

**Status: not started · Priority: P1 · Depends on: Phase 1 · Improves with Phases 2 and 4**

Brainrot cannot currently test itself. `tests/test_brainrot.py` runs every
`test_cases/*.brainrot` from the outside and string-matches stdout against
`tests/expected_results.json`; the only in-language assertion is `bet`, which
aborts the process on failure and reports nothing else. `sussybaka` is the
missing half: an in-language unit testing library where you register tests,
assert things, and get a report instead of a corpse.

The name is the whole design brief. You are suspicious of your code, so you
check it.

This phase is P1 rather than P2 because it is not a side quest: the project's own
test suite gets rewritten on top of it (§9c), which makes it load-bearing
infrastructure rather than a nice-to-have.

Three lettered work items — **9a** delivery, **9b** mocking, **9c** the suite
rewrite — with the design they share described in the unlettered sections
between them.

### Shape: a library, not keywords

Almost every operation here is "a call with arguments", which is exactly the case
Appendix A says should stay an ordinary library function — keyword status would
buy no syntax and cost a reserved identifier forever. The one exception is `npc`,
the function-reference type that `larping` needs (§9b); that is a genuine type,
so it is a genuine keyword.

```c
#cooked <sussybaka>

skibidi sus_adds_two_numbers() {
    fr(add(2, 2), 4);
    nah(add(2, 2), 5);
}

skibidi sus_area_is_roughly_pi() {
    lowkey(circle_area(1.0), 3.14159, 0.0001);
}

skibidi sus_divide_by_zero_explodes() {
    ragebait(divide(1, 0));
}

skibidi main {
    sussybaka("adds two numbers",     sus_adds_two_numbers);
    sussybaka("area is roughly pi",   sus_area_is_roughly_pi);
    sussybaka("divide by zero blows", sus_divide_by_zero_explodes);
    bussin roasted();
}
```

### 9a. Delivery: `#cooked <sussybaka>`

`sussybaka` is **not** globally available the way `yapping` and `bet` are. You
opt in by cooking it, which keeps twelve common words out of every program's
namespace and gives the library a place to live that isn't "more builtins".

That requires the angle-bracket form of the directive, which does not exist yet:
[lang.l:406](../lang.l#L406) matches only `#cooked "path"`, and anything else
falls through to the malformed-directive rule at [lang.l:415](../lang.l#L415) and
dies. So this phase owns a small, self-contained extension:

| Form | Meaning | Status |
| ---- | ------- | ------ |
| `#cooked "path/to/file.brainrot"` | textual include, resolved relative to the including file | **implemented** |
| `#cooked <name>` | resolve `name` on the **module search path** | new in this phase |

Search-path resolution tries, in order:

1. `$BRAINROT_PATH` entries, if set.
2. The install prefix's library directory (`$PREFIX/lib/brainrot/`).
3. The in-tree `stdrot/` directory, so a build from source works with no install
   step — which the test-suite rewrite in §9c depends on.

A resolved name may be either a `.brainrot` **prelude** or, once Phase 4 lands, a
native `.so` **module**. That is deliberate: one syntax, one search path, two
possible artifact kinds. It also settles what the roadmap previously left vague —
Phase 4's `#cooked <raylib>` and this phase's `#cooked <sussybaka>` are the same
mechanism, and Phase 4 becomes "teach the existing resolver about `.so` files"
rather than a whole new directive.

**How v1 splits:**

- `stdrot/sussybaka.c` — the primitives, in `libstdrot.so`: the test registry,
  result recording, comparison helpers, the timer, and report formatting. Not
  intended to be called directly.
- `stdrot/sussybaka.brainrot` — the surface, resolved by `#cooked <sussybaka>`:
  the twelve names in the vocabulary table as thin `skibidi` wrappers, plus suite
  bookkeeping.

Programs only ever see the prelude. When Phase 4 arrives, prelude and primitives
collapse into a real module and **not one line of user code changes** — the
`#cooked <sussybaka>` line already says the right thing.

### Vocabulary

| Op | Function | Mnemonic |
| -- | -------- | -------- |
| register a test | `sussybaka` | you're suspicious of it, so you check it |
| assert equal | `fr` | fr fr — it really is that |
| assert not equal | `nah` | it's not that |
| assert condition holds | `zesty` | it's giving correct |
| assert condition fails | `capping` | it's cap, and you called it |
| assert near (floats) | `lowkey` | approximately, roughly |
| expect an abort/crash | `ragebait` | you provoked it on purpose |
| skip a test | `touchgrass` | not today |
| fail unconditionally | `mogg` | you just lost, unconditionally |
| install a mock | `larping` | this function is pretending to be another one |
| setup hook | `lockin` | lock in before each test |
| teardown hook | `logoff` | log off after each test |
| suite summary | `roasted` | your year in review |

`capping` asserts a condition is **false** and `zesty` asserts it is **true** —
the polarity follows the slang, where capping is lying.

None of these collide with a keyword in `lang.l`, with a registered builtin
(`yapping`, `yappin`, `baka`, `bet`, `chill`, `ragequit`, `slorp`), or with a
name proposed elsewhere in this roadmap. `mogg` is deliberately distinct from
Phase 6's `mogged`, and `sussybaka` from Phase 7's `sus`.

### Relationship to `bet`

`bet` is untouched. It stays the bare runtime assertion that aborts, and
`test_cases/bet.brainrot` / `bet_fail.brainrot` keep passing unchanged.
`sussybaka`'s assertions are a separate family with different semantics: they
record a result and abort only the *current test*. No compatibility surface
moves in this phase.

### Runner mode

```bash
./brainrot --sus tests/sus_math.brainrot
```

`main()` at [lang.y:748](../lang.y#L748) hard-rejects `argc != 2`, so generalizing
argument parsing is part of this phase.

There is a tension to resolve up front: library-only registration means
*something* has to execute the `sussybaka(...)` calls, but test bodies should
carry no harness boilerplate. The resolution is that in `--sus` mode `main` is a
**manifest, not a driver** — the runner executes `main` purely to collect
registrations, then takes over and drives the tests itself, applying isolation,
timing, output formatting, and the exit code. The test functions themselves stay
plain `skibidi` functions.

Registration takes a **function reference** (`npc`, §9b), so a typo is a semantic
error at analysis time rather than a runtime lookup miss. If `npc` slips, the
fallback is registration by name string — `sussybaka("adds two", "sus_add")`
resolved through the existing function table — which works today but moves typos
to run time.

Semantics:

1. Parse and analyze as usual.
2. Run `main` to collect the manifest.
3. For each registered test: `lockin` → body → `logoff`.
4. Print the summary; exit `0` if and only if every test passed.

### Failure isolation

An assertion failure aborts the current test and the runner continues with the
next one. Mechanically this reuses the existing `setjmp` machinery: the runner
pushes a jump buffer ([ast.h:547](../ast.h#L547)) before each test body and a
failing assertion `longjmp`s back into it.

Two hazards, both of which need explicit handling and tests:

- The jump-buffer stack is shared with `bruh`/loop handling, so a test that
  aborts from inside a loop leaves frames behind. The runner must unwind the
  stack back to its own frame, not merely pop one entry.
- Scope and arena state from an aborted test must not leak into the next one.
  This is the part most likely to produce ASan findings, so it gets tests before
  it gets features.

### Output

Human-readable by default; TAP version 13 behind a flag, so CI and the existing
pytest harness can consume results without parsing prose.

```
sussybaka: sus_math.brainrot
  ✓ adds two numbers                    0.4ms
  ✗ area is roughly pi                  0.3ms
      lowkey: expected 3.14159 ± 0.0001, got 3.14158999
      at sus_math.brainrot:12
  ~ divide by zero blows               skipped (touchgrass)

roasted: 1 passed, 1 failed, 1 skipped in 1.2ms
```

### In scope for this phase

- **Core.** Registration, the assertion family above, per-test isolation,
  summary, exit code.
- **`lockin` / `logoff` hooks.** Run before/after each test in a suite.
- **Suites.** Grouping registered tests under a named suite, so hooks and the
  summary have a scope smaller than "the file".
- **Skip and expected-failure.** `touchgrass` marks a test skipped;
  `ragebait` asserts that an expression aborts, which is how you test
  `ragequit`, `baka`, and division by zero.
- **Parameterized tests.** Same body, a table of inputs, one result line each.
  Cheapest form is an array of cases the body indexes; a real table syntax wants
  Phase 3's aggregates.
- **Per-test timing.** Monotonic clock around each body, reported in the summary.
- **Mocking.** `larping` plus first-class functions — see §9b, which is the
  largest single piece of work in this phase.

### 9b. `larping` and first-class functions (`npc`)

Mocking means substituting one function for another, which means a function has
to be a *value*. Brainrot has no such thing today, so this phase introduces one.

```c
npc  /* a function reference: hand someone a script and they run it */
```

`npc` is the mnemonic pair to `larping` — an NPC runs whatever script it was
given, and `larping` is how you hand it a different one. Minimum viable feature
set:

- `npc` as a type in declarations and parameters.
- A bare function name in value position evaluates to a reference to it.
- Calling through an `npc`-typed variable: `f(args)` where `f` is an `npc`.
- Assignment and equality comparison of references.

Not in scope: closures, capture, anonymous functions, returning `npc` from
native code. Those are a language-design project; this is a pointer to a
`FunctionDef`.

```c
#cooked <sussybaka>

skibidi rizz fake_roll() {
    bussin 4;   /* chosen by fair dice roll, guaranteed random */
}

skibidi sus_player_moves_four_spaces() {
    larping(roll_dice, fake_roll);   /* roll_dice now LARPs as fake_roll */
    fr(take_turn(), 4);
}                                    /* restored when the test ends */
```

**Mechanism.** `larping(real, stub)` rebinds the callee `real` resolves to, for
the duration of the current test, and the runner restores it afterwards. Two
constraints that are easy to get wrong:

- **Restoration must survive an abort.** A failing assertion `longjmp`s out of
  the test body, so restoration cannot live at the end of the body. The runner
  owns a per-test list of installed mocks and unwinds it in the same place it
  unwinds the jump-buffer stack — the one code path that runs whether the test
  passed, failed, or aborted.
- **Signature compatibility.** `larping` should reject a stub whose signature
  does not match the target. Fully checkable once Phase 2 lands; before that it
  is an arity check plus a runtime type check at the call.

**This is bigger than testing.** Function references unlock raylib callbacks,
`yeet worker` taking a function rather than a syntactic call, socket handlers in
Phase 8, and comparator arguments for any future sort. That is an argument for
promoting `npc` out of Phase 9 into its own phase — see Appendix B.

### 9c. Rewriting the project's test suite

The end state is that `test_cases/` stops being a set of programs whose
correctness lives in a separate JSON file, and becomes a set of programs that
state their own expectations.

Today:

```c
/* test_cases/add_two_numbers.brainrot */
skibidi main { yapping("%d", 2 + 2); }
```
```json
"add_two_numbers": "4"
```

After:

```c
#cooked <sussybaka>

skibidi sus_addition() { fr(2 + 2, 4); }
skibidi main { sussybaka("addition", sus_addition); bussin roasted(); }
```

The expectation moves next to the code, a failure says which assertion broke
instead of diffing two blobs of stdout, and a test file is runnable on its own.

**Migration must be incremental — 97 files do not move in one commit.**

1. Land Phase 9 core and add `test_cases/sussybaka_*.brainrot` alongside the
   existing suite. Nothing migrates yet.
2. Teach `tests/test_brainrot.py` a second mode: a case with no
   `expected_results.json` entry is run under `--sus --tap` and asserted on exit
   code and TAP output instead of a stdout string. The two styles coexist.
3. Migrate in batches by area (arrays, structs, `#cooked`, string builtins),
   deleting each `expected_results.json` entry as its case converts.
4. When the JSON is empty, delete it and simplify the harness.

Two things that must keep working throughout:

- `run_valgrind_tests.sh` iterates `test_cases/*.brainrot` and runs each under
  valgrind. Migrated tests are still ordinary `.brainrot` files, so this keeps
  working unchanged — but it needs to pass `--sus`, and a *deliberately failing*
  sussybaka case must stay leak-clean or `make valgrind` goes red for the wrong
  reason.
- Error-path cases (`bet_fail`, `division_by_zero`, `cooked_missing`,
  `cooked_circular`, and friends) assert on **stderr and a non-zero exit code**.
  Those do not become sussybaka tests — the process is supposed to die. They stay
  string-matched, so the JSON harness may never fully disappear, and step 4 above
  should be treated as aspirational rather than committed.

`AGENTS.md`'s testing requirement ("a `.brainrot` file in `test_cases/`, a
corresponding entry in `tests/expected_results.json`") is written against the
current layout and must be updated in the same PR as step 2.

### Wishlist (not this phase)

- **Leak assertions.** "Assert this block allocates nothing it doesn't free"
  fits the project's ASan/valgrind-first culture perfectly, but needs allocator
  instrumentation that doesn't exist yet.
- **Closures and anonymous functions.** Once `npc` exists, someone will want
  `larping(roll_dice, skibidi() { bussin 4; })`. Deliberately excluded from §9b.

### Dependencies

The library surface is buildable today in a degraded, statement-only form. The
phase as scoped needs three things it does not have:

- **Angle-bracket `#cooked` + a search path** (§9a). New, small, owned here.
- **`npc` function references** (§9b). New, not small, and arguably its own phase.
- **Phase 1** — assertions become expressions, so `zesty(fr(a, b))` composes and
  an assertion can be used as a value rather than only as a statement.

It gets better with **Phase 2** (typed signatures let the analyzer reject
`fr("string", 42)` at analysis time, and make `larping`'s signature check exact)
and with **Phase 4**, which upgrades the prelude to a real module without any
program changing.

Nothing in Phases 1–8 depends on this, and it does not block them.

### Definition of done

- `#cooked <sussybaka>` resolves through the module search path, with tests for
  a missing module, a search-path hit, and the in-tree fallback.
- `stdrot/sussybaka.c` primitives registered through `STDROT_EXPORT`, with
  `stdrot/sussybaka.brainrot` as the cooked surface.
- `npc` in the grammar, the analyzer, and the interpreter; `larping` installing
  and restoring mocks, including on the abort path.
- `--sus` runner mode with human and TAP output.
- `test_cases/sussybaka_pass.brainrot`, `sussybaka_fail.brainrot`,
  `sussybaka_skip.brainrot`, `sussybaka_larping.brainrot`, and
  `sussybaka_abort_isolation.brainrot` (a test that fails inside a loop with a
  mock installed, proving both that the next test still runs and that the mock
  was restored), each with an entry in `tests/expected_results.json`.
- One migrated batch of the existing suite, proving the §9c path end to end.
- `docs/` reference page for the assertion vocabulary; README keyword table
  updated for `npc`.
- `make test` and `make valgrind` green — including under a deliberately failing
  suite, since the abort path is the one most likely to leak.

---

## Phase 10 — File I/O

**Status: not started · Priority: P2 · Depends on: Phase 1**

`stdio.h`, rebranded. Like `sussybaka`, this is a **library, not keywords** —
every operation here is "a call with arguments", so it belongs in `stdrot`
alongside `yapping`, `slorp`, and `chill`, not in `lang.l`. Unlike `sussybaka`
it is not gated behind `#cooked` — file I/O is common enough to stay globally
available, the same way `slorp` is today. `stdrot/file.c` self-registers the
functions below the same way `baka.c` and `yapping.c` do.

**Type:** `FILE *` → **`SAUCE *`**

| C | Brainrot | Why |
| - | -------- | --- |
| `fopen` | `crackopen` | you crack open the file |
| `fclose` | `peaceout` | perfect opposite lifecycle |
| `fread` | `doomscroll` | consuming data endlessly |
| `fwrite` | `shitpost` | putting content into the file |
| `fgets` | `skim` | silently reads a line |
| `fputs` | `yapto` | yapping into a file |
| `fseek` | `zoink` | literally moving through the file |
| `ftell` | `whereami` | cursor position |
| `rewind` | `throwback` | self-explanatory |
| `feof` | `itsjoever` | **this one is mandatory** |
| `ferror` | `bricked` | file operation got bricked |
| `fflush` | `bustcache` | flush buffered output |

`yapto` is deliberately distinct from raw `shitpost`: `shitpost` is
`fwrite`-shaped (size/count, binary-safe), `yapto` is `fprintf`-shaped (a
format string). That mirrors the existing split between `yapping` (stdout,
formatted, newline) and `yappin` (stdout, formatted, no newline). None of
these twelve names collide with `lockin`/`logoff` (Phase 9's sussybaka hooks)
or `lurk` (Phase 8's `listen()`) — an earlier draft of this table did use
those three names, which is why the family was renamed wholesale rather than
resolved name-by-name.

```c
skibidi main {
    SAUCE *f = crackopen("classified_lore.txt", "r");
    edgy (!f) {
        yapping("file got negative aura");
        ragequit(1);
    }

    goon (!itsjoever(f)) {
        rant line = skim(f);
        yapping("%s", line);
    }

    peaceout(f);
    bussin 0;
}
```

```c
SAUCE *manifesto = crackopen("schizo.txt", "w");
yapto(manifesto, "aura = %d", aura);
peaceout(manifesto);
```

### Work

- A `SAUCE *handle type wrapping `FILE *`, following the same handle pattern
  the ABI work in Phase 2 formalizes (`STDROT_HANDLE`) — this phase can ship
  ahead of Phase 2 with a narrower, file-only handle representation and adopt
  the general one once it lands.
- `stdrot/file.c` implementing the twelve functions above via `STDROT_EXPORT`.
- Since `SAUCE *f = crackopen(...)` needs the return value in an initializer,
  this phase is blocked on Phase 1 exactly the way `sussybaka` is.

### Definition of done

- `test_cases/file_io.brainrot` covering open/read/write/close and the
  `itsjoever` loop idiom above, plus a missing-file case (`crackopen`
  returning a negative/falsy handle) exercised through `edgy (!f)`.
- No leaked `FILE *` on any exit path — `make valgrind` is the relevant gate,
  since an unclosed handle is exactly the kind of bug it exists to catch.
- `docs/` reference page for the file I/O vocabulary; README keyword table
  is **not** touched, since none of these are grammar keywords.

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
| **M7 — Concurrency** | Phase 6 | `yeet` / `mogged`. |
| **M8 — Data structures** | Phase 7 | `grindset`. |
| **M9 — Network** | Phase 8 | Brainrot serves HTTP. |
| **M10 — Brainrot tests itself** | Phase 9a/9b | `#cooked <sussybaka>`, `npc`, `larping`, `./brainrot --sus`. |
| **M11 — The suite is self-describing** | Phase 9c | `test_cases/` states its own expectations. |
| **M12 — Files** | Phase 10 | `crackopen`/`peaceout`/`itsjoever` and the rest of the file I/O family. |

M1–M4 are strictly ordered. M7, M8, M10, and M12 are independent of M2–M6 and
can proceed in parallel by anyone who wants them. M11 depends only on M10.

M10 quietly delivers two things the rest of the roadmap wants: the module search
path that Phase 4 builds on, and the function references that Phases 5, 6, and 8
all need for callbacks. That is the real reason it is P1.

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
| `mogged` | join thread | 6 | proposed |
| `gatekeep` | mutex / lock | 6 | proposed |
| `letcook` | unlock | 6 | proposed |
| `simp` | condition wait | 6 | proposed |
| `yass` | condition signal | 6 | proposed |
| `ratioed` | condition broadcast | 6 | optional |
| `grindset` | hashmap | 7 | proposed |
| `ship` | map insert | 7 | proposed |
| `blorbo` | map lookup | 7 | proposed |
| `sus` | map contains | 7 | proposed |
| `unalive` | map delete | 7 | proposed |
| `bagsize` | map size | 7 | proposed |
| `drip` | socket | 8 | proposed |
| `brat` | bind | 8 | proposed |
| `lurk` | listen | 8 | proposed |
| `snatched` | accept | 8 | proposed |
| `stan` | connect | 8 | proposed |
| `dm` | send | 8 | proposed |
| `peep` | recv | 8 | proposed |
| `ghost` | close | 8 | proposed |
| `npc` | function reference type | 9 | proposed |

Every keyword here is a single word. Brainrot does have precedent for multi-word
keywords — `"sigma rule"` lexes as `case` at [lang.l:72](../lang.l#L72) — but
phrases read badly in call position (`soft launch(server, 8080)`), so the
vocabulary stays single-token throughout.

None of these collide with a keyword currently in `lang.l`, with a registered
builtin (`yapping`, `yappin`, `baka`, `bet`, `chill`, `ragequit`, `slorp`), or
with a proposed preprocessor directive. `letcook` is deliberately one word so it
cannot be confused with the `#cooked` directive.

`bruh` was considered and rejected: it is already `break`
([lang.l:71](../lang.l#L71)).

Per `AGENTS.md`, the README keyword table is a public compatibility surface.
These are additive, but the table must be updated in the same PR that implements
each keyword, and any change to an *existing* keyword needs explicit sign-off.

`npc` is the only Phase 9 addition in this table. The rest of the `sussybaka`
vocabulary — `sussybaka`, `fr`, `nah`, `zesty`, `capping`, `lowkey`, `ragebait`,
`touchgrass`, `mogg`, `larping`, `lockin`, `logoff`, `roasted` — is deliberately
**absent**: it is a cooked library, so those are ordinary function names, not
reserved words. They still must not collide, and all thirteen were checked
against `lang.l`, the registered builtins, and the proposals above — but they
cost the grammar nothing, they only exist in files that say
`#cooked <sussybaka>`, and they never appear in the README keyword table. They
are the worked example of the rule in the next paragraph; `npc` is the worked
example of the exception, because a type needs a spelling and no amount of
library design will give it one.

Several of these could reasonably be library functions instead of keywords —
`ship`/`blorbo`/`dm`/`peep` in particular. Keyword status buys syntax; it costs
grammar complexity and a reserved identifier forever. A useful split: operations
that need statement syntax (`yeet`, `mogged`) earn keyword status, while anything
that is just a call with arguments (`ship`, `blorbo`, `dm`, `peep`) can stay an
ordinary library function and keep the joke without touching the grammar. Decide
per keyword, and default to "library function" when in doubt.

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
8. **Should `npc` be its own phase (Phase 9b)?** Function references are a
   language feature that Phases 5, 6, and 8 all want for callbacks, and they are
   the largest item inside a phase otherwise made of library code. Leaving them
   in Phase 9 means the testing library is gated on a grammar change; hoisting
   them out means Phase 9 ships name-string registration first and gains
   `larping` later. **Recommendation: hoist**, if anyone other than `sussybaka`
   asks for them first.
9. **Test discovery (Phase 9).** Registration-by-manifest in `main` versus
   convention-based auto-discovery (`--sus` runs every zero-arg `skibidi`
   function named `sus_*`). The manifest is explicit and needs no new machinery;
   auto-discovery removes the last piece of boilerplate but makes the runner
   depend on a naming convention.
10. **Mock restoration on abort (Phase 9b).** `larping` stubs must be restored
    even when a test `longjmp`s out mid-way. Does that need a general
    "unwind actions" mechanism in the interpreter, which nothing else currently
    has, or is a runner-owned restore list enough? The general mechanism would
    also serve `logoff`, `gatekeep`/`letcook` (Phase 6), and `ghost` (Phase 8),
    which all have the same "must run even on abnormal exit" shape.
11. **Module search path (Phase 9a).** `$BRAINROT_PATH` + install prefix +
    in-tree `stdrot/` is proposed. Does the in-tree fallback apply always, or
    only for an uninstalled build? Getting this wrong means a system install
    silently shadows the working tree, or vice versa.
12. **Prelude versus builtins (Phase 9a).** The split is "primitives in
    `libstdrot.so`, surface in a cooked `.brainrot` prelude". The primitives are
    still globally visible builtins even when nobody cooks `sussybaka` — do they
    need a naming convention (`__sus_record`) to signal "not for you", or does
    the language need real module-private names, which nothing else has?
13. **How far does the §9c migration actually go?** Error-path cases assert on
    stderr and a non-zero exit code, so they cannot become sussybaka tests. That
    means `tests/expected_results.json` probably survives forever in reduced
    form. Is a permanently two-mode harness acceptable, or should the error-path
    cases get their own mechanism (`ragebait` at file scope?) so the JSON can
    genuinely be deleted?
14. **`AGENTS.md` is a contract.** It currently requires an
    `expected_results.json` entry for every test. Step 2 of §9c makes that false.
    Update it in the same PR, or add the sussybaka mode alongside it and remove
    the JSON requirement only when the migration completes?
