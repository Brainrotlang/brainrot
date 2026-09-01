/* stdrot/gamba.c – Cryptographically safe random numbers for libstdrot.so
 *
 * `gamba` is the CSPRNG. It never wraps C's rand()/random()/srand(), never
 * seeds from a clock, and never uses the modulo-bias shortcut. Bytes come
 * straight from OpenSSL's RAND_bytes, and a RAND_bytes return other than 1
 * is a hard, immediate abort -- a CSPRNG failure is not a value, so it is
 * never silently rounded down to a look-alike 0.
 *
 *   gamba()       -- unbiased rizz in [0, INT_MAX]; the rand() shape, honest.
 *   gamba(n)      -- unbiased rizz in [0, n); so nobody writes gamba() % n.
 *   gamba(lo, hi) -- unbiased rizz in [lo, hi], inclusive; dice/damage rolls.
 *
 * There is deliberately no gamba_seed: OpenSSL seeds itself, and a seed knob
 * would be a security bug dressed as an API. gamba_bytes(buf, n) is described
 * in the issue but deferred here -- see the note above STDROT_EXPORT_SIG.
 *
 * ── Build split ────────────────────────────────────────────────────────────
 * The native libstdrot.so build links OpenSSL's libcrypto (Makefile) and this
 * file uses RAND_bytes. The wasm build (-DSTDROT_STATIC, see stdrot.c and the
 * Makefile) must stay OpenSSL-free (issue #175), so under STDROT_STATIC gamba
 * is a documented stub that errors instead of pulling in a weaker generator.
 * A Web Crypto / getentropy backend for wasm is a follow-up, not this phase.
 * There is intentionally NO #ifdef that swaps in rand() on native: a missing
 * OpenSSL fails the native link rather than compiling a weaker gamba.
 */

#include "stdrot_api.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Abort helper shared by every gamba form: report against the current
 * execution context (line number) and exit, matching bet()'s failure style. */
static void gamba_die(const char *reason)
{
    fprintf(stderr, "Error: gamba: %s at line %d\n", reason,
            g_exec_context.line_number);
    exit(1);
}

#if defined(_WIN32)

/* Windows: draw from the OS CSPRNG via BCryptGenRandom (bcrypt.dll, always
 * present), so gamba is a real CSPRNG without an OpenSSL dependency -- and
 * without the STDROT_STATIC stub below, which the Windows build would
 * otherwise hit (it compiles with -DSTDROT_STATIC). Checked before
 * STDROT_STATIC on purpose. BCRYPT_USE_SYSTEM_PREFERRED_RNG lets us pass a
 * NULL algorithm handle. NTSTATUS success is STATUS_SUCCESS (0). */
#include <windows.h>
#include <bcrypt.h>
static uint64_t gamba_random_u64(void)
{
    unsigned char bytes[sizeof(uint64_t)];
    if (BCryptGenRandom(NULL, bytes, (ULONG)sizeof(bytes),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    {
        gamba_die("CSPRNG failure (BCryptGenRandom did not succeed)");
    }
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(bytes); i++)
    {
        value = (value << 8) | bytes[i];
    }
    return value;
}

#elif defined(STDROT_STATIC)

/* wasm / static build: no OpenSSL. gamba errors loudly rather than silently
 * degrading to a non-cryptographic source. Native stdlib always has a real
 * gamba; this stub exists only where linking libcrypto is intentionally off. */
static uint64_t gamba_random_u64(void)
{
    gamba_die("CSPRNG unavailable in this build (no OpenSSL)");
    return 0; /* unreachable: gamba_die() exits */
}

#else

#include <openssl/rand.h>

/* One 64-bit draw from the CSPRNG, or a hard abort. RAND_bytes returning
 * anything but 1 means the generator failed -- never a usable roll. */
static uint64_t gamba_random_u64(void)
{
    unsigned char bytes[sizeof(uint64_t)];
    if (RAND_bytes(bytes, (int)sizeof(bytes)) != 1)
    {
        gamba_die("CSPRNG failure (RAND_bytes did not return 1)");
    }
    uint64_t value = 0;
    for (size_t i = 0; i < sizeof(bytes); i++)
    {
        value = (value << 8) | bytes[i];
    }
    return value;
}

#endif /* STDROT_STATIC */

/* Unbiased uniform integer in [0, bound) for bound > 0, via rejection
 * sampling -- the OpenBSD arc4random_uniform algorithm. `-bound` on an
 * unsigned uint64_t is 2^64 - bound, so `min` is 2^64 mod bound: the count
 * of low values that would skew a plain modulo. Discarding draws below that
 * threshold leaves exactly (2^64 - min) candidates, an integer multiple of
 * bound, so `draw % bound` is uniform. No modulo bias, no clock, no rand(). */
static uint64_t gamba_uniform(uint64_t bound)
{
    uint64_t min = (uint64_t)(-bound) % bound;
    uint64_t draw;
    do
    {
        draw = gamba_random_u64();
    } while (draw < min);
    return draw % bound;
}

static StdrotValue gamba_int_result(int value)
{
    StdrotValue result = {STDROT_INT, {0}};
    result.val.i = value;
    return result;
}

/* Coerce one marshalled argument to int. The signature declares STDROT_INT
 * params, so the semantic analyzer already rejects non-integer arguments;
 * this stays defensive about the exact scalar tag the ABI hands back. */
static int gamba_arg_to_int(const StdrotValue *arg)
{
    switch (arg->type)
    {
    case STDROT_INT:
        return arg->val.i;
    case STDROT_SHORT:
        return arg->val.s;
    case STDROT_BOOL:
        return arg->val.b ? 1 : 0;
    case STDROT_CHAR:
        return (int)arg->val.c;
    default:
        gamba_die("arguments must be integers");
        return 0; /* unreachable */
    }
}

/* gamba(), gamba(n), gamba(lo, hi) -- dispatched on argc. The registry
 * signature (min_args 0, param_count 2, non-variadic) guarantees argc is
 * 0, 1, or 2 for any program that passed semantic analysis. */
static StdrotValue stdrot_gamba(StdrotValue *args, int argc)
{
    if (argc <= 0)
    {
        /* gamba(): unbiased value in [0, INT_MAX] inclusive. */
        uint64_t span = (uint64_t)INT_MAX + 1;
        return gamba_int_result((int)gamba_uniform(span));
    }

    if (argc == 1)
    {
        /* gamba(n): unbiased value in [0, n). */
        int n = gamba_arg_to_int(&args[0]);
        if (n <= 0)
        {
            gamba_die("gamba(n) requires n > 0");
        }
        return gamba_int_result((int)gamba_uniform((uint64_t)n));
    }

    /* gamba(lo, hi): unbiased value in [lo, hi] inclusive. */
    int lo = gamba_arg_to_int(&args[0]);
    int hi = gamba_arg_to_int(&args[1]);
    if (hi < lo)
    {
        gamba_die("gamba(lo, hi) requires hi >= lo");
    }
    /* Width computed in 64-bit so hi - lo can't overflow (INT_MAX - INT_MIN
     * exceeds int), then +1 for an inclusive upper bound. */
    uint64_t span = (uint64_t)((int64_t)hi - (int64_t)lo) + 1;
    return gamba_int_result((int)((int64_t)lo + (int64_t)gamba_uniform(span)));
}

/* gamba(rizz lo, rizz hi) -> rizz, with 0, 1, or 2 arguments all accepted:
 * min_args 0 (the no-arg form), param_count 2 (both optional-but-typed as
 * rizz), non-variadic (no unchecked tail). Not identity-polymorphic and not
 * C-variadic -- the return is always a fixed rizz.
 *
 * gamba_bytes(buf, n) from the issue is intentionally NOT registered here:
 * filling a caller buffer with raw random bytes (including embedded NULs)
 * needs a mutable-pointer/output-buffer contract this ABI doesn't cleanly
 * express yet -- STDROT_STRING is length-prefixed and copied at the
 * boundary. Deferred to a follow-up rather than shipped half-safe; the three
 * integer forms are the crypto-safe surface this phase guarantees. */
static const StdrotParam gamba_params[] = {
    {STDROT_INT, NULL, 0},
    {STDROT_INT, NULL, 0},
};

STDROT_EXPORT_SIG("gamba", stdrot_gamba, ((StdrotParam){STDROT_INT, NULL, 0}),
                  gamba_params, 2, 0, false);
