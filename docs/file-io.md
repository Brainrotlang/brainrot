# File I/O — `stdio.h`, rebranded

`stdio.h` with the serial numbers filed off. Twelve operations behind a
`SAUCE *` handle.

These are **library functions, not keywords** — every one of them is "a call
with arguments", so they live in `stdrot/` alongside `yapping`, `slorp` and
`gamba`. They are **not** gated behind `#cooked`: file I/O is common enough to
stay globally available, the same way `slorp` is. Nothing here appears in the
README's keyword table, because none of it is grammar.

The one exception is the type name `SAUCE`, which *is* lexed — it names a type,
not an operation.

---

## Vocabulary

| C | Brainrot | Why |
| - | -------- | --- |
| `fopen` | `crackopen` | you crack open the file |
| `fclose` | `peaceout` | perfect opposite lifecycle |
| `fread` | `doomscroll` | consuming data endlessly |
| `fwrite` | `shitpost` | putting content into the file |
| `fgets` | `skim` | silently reads a line |
| `fputs`/`fprintf` | `yapto` | yapping into a file |
| `fseek` | `zoink` | literally moving through the file |
| `ftell` | `whereami` | cursor position |
| `rewind` | `throwback` | self-explanatory |
| `feof` | `itsjoever` | **this one is mandatory** |
| `ferror` | `bricked` | file operation got bricked |
| `fflush` | `bustcache` | flush buffered output |

## Signatures

```c
SAUCE *crackopen(rant path, rant mode);   /* NULL handle if it won't open  */
rizz   peaceout(SAUCE *f);                /* 0 on success, like fclose     */

rant   skim(SAUCE *f);                    /* one line, newline stripped    */
rant   doomscroll(SAUCE *f, rizz n);      /* up to n bytes, binary-safe    */
skibidi yapto(SAUCE *f, rant fmt, ...);   /* formatted, like fprintf       */
rizz   shitpost(SAUCE *f, rant data);     /* raw bytes; returns the count  */

rizz   zoink(SAUCE *f, rizz off, rizz whence);  /* 0=start 1=cur 2=end     */
rizz   whereami(SAUCE *f);                /* byte offset, or -1            */
skibidi throwback(SAUCE *f);              /* rewind; also clears the flags */

cap    itsjoever(SAUCE *f);               /* nothing left to read?         */
cap    bricked(SAUCE *f);                 /* did something go wrong?       */
rizz   bustcache(SAUCE *f);               /* flush; 0 on success           */
```

---

## The read loop

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

This prints exactly the file's lines — no trailing blank.

That is worth calling out, because the equivalent C loop is famously wrong.
`feof()` reports that a read has *already* failed, so `while (!feof(f))` runs
one iteration too many and processes a phantom empty record. **`itsjoever`
peeks instead**: it looks at the next byte and puts it back, so it answers the
question its name asks — *is there anything left* — rather than *did something
already fail*. The peek is invisible: the stream position and flags are
unchanged afterwards.

`bricked` is a plain `ferror` by contrast, because "did something go wrong" is
a question C's semantics answer correctly.

## Writing

`yapto` and `shitpost` are both write operations and are **not**
interchangeable:

```c
SAUCE *manifesto = crackopen("schizo.txt", "w");

yapto(manifesto, "aura = %d\n", aura);   🚽 formatted text, like fprintf
shitpost(manifesto, "\x00\x01raw\n");    🚽 exact bytes, like fwrite

peaceout(manifesto);
```

`yapto` takes a format string; `shitpost` writes a `rant`'s bytes verbatim and
returns how many it wrote. This mirrors `yapping`/`yappin`'s relationship to
stdout — and `yapto` shares their exact formatter, so the three cannot drift
apart.

`shitpost` and `doomscroll` are **binary-safe**: they work in byte counts, not
terminators, so a `rant` containing an embedded NUL round-trips unchanged.
That is what the length prefix on a `rant` is for.

## A missing file is falsy, not fatal

```c
SAUCE *f = crackopen("maybe.txt", "r");
edgy (!f) {
    yapping("not there");
}
```

`crackopen` returns a null handle when it cannot open the file, and a null
pointer is falsy — so `edgy (!f)` is the idiomatic check. This is deliberately
*not* an error: a missing file is an ordinary outcome a program should be able
to handle.

Being handed an **invalid** handle is a different matter, and is fatal — see
below.

---

## What a `SAUCE *` actually is

An opaque token. Not a pointer you can dereference, not a number to do
arithmetic on — the address identifies a resource that the *library* owns.

This is Brainrot's answer to a question the roadmap left open (Appendix B Q6:
*"Textures, sockets, and map entries all outlive statements. Brainrot has no
destructors and no GC. Handles sidestep this by keeping ownership in C — is
that the general answer?"*). It is, and files are the simplest case to prove it
on:

1. **Ownership stays in C.** The library creates the resource and owns it.
   Brainrot never holds anything it could free, so there is no Brainrot-side
   `free` to get wrong.
2. **Release is manual** — `peaceout(f)` — because Brainrot has no destructors
   and no GC.
3. **The library keeps a registry of live handles** and validates every handle
   against it. This is the part that matters.
4. **Anything still open at shutdown is closed by the library**, so a program
   that exits without cleaning up does not leak.

Point 3 is why a handle is genuinely safer than the raw pointer underneath it.
A program can produce an address that was never a file — a stale token kept
past `peaceout`, or something from elsewhere entirely — and the type system
cannot tell, because it sees only "opaque pointer". Passing one to `fclose`
would be undefined behaviour. Instead:

```c
SAUCE *f = crackopen("lore.txt", "r");
peaceout(f);
skim(f);     🚽 Error: skim: not an open SAUCE -- it was never opened,
             🚽 or was already closed with peaceout
```

Use-after-release and double-release are **diagnosed**, not undefined. So is
operating on the null handle from a failed `crackopen`.

Point 4 means this leaks nothing, even though it never closes anything:

```c
skibidi main {
    SAUCE *f = crackopen("lore.txt", "r");
    yapping("opened, never closed");
    ragequit(3);      🚽 exits immediately; no cleanup code runs
}
```

The library closes it on unload. `make valgrind` checks this for real — the
sweep tracks file descriptors, not just allocations, because a `FILE *` that
was never closed does **not** show up as a memory leak: glibc keeps every open
stream on its own list, so the allocation stays reachable to the very end.

## Limits of v1

- **`SAUCE` is only ever written with a star.** `SAUCE *f` is the handle; a
  bare `SAUCE` is not a value the type system can represent.
- `whereami` and `zoink` take `rizz` offsets, so files beyond 2 GB are out of
  reach for now.
- There is no `remove`/`rename`, no directory listing, and no `stat`.

## See also

- [`test_cases/file_io.brainrot`](../test_cases/file_io.brainrot) — every
  operation, plus the read-loop idiom.
- [`examples/file_wordcount.brainrot`](../examples/file_wordcount.brainrot) —
  a worked example.
- `STDROT_HANDLE` in [`stdrot/stdrot_api.h`](../stdrot/stdrot_api.h) — the ABI
  contract, for anyone adding a second kind of handle.
