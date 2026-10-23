// Guard-page allocator for debug builds (guard_heap.h; make GUARD_HEAP=1, Windows). Blocks come from one reserved
// range, bump-allocated and never reused: [header page(s) ... block | guard page]. The block ends at its last page's
// end (rounded to 16 bytes, malloc's alignment), so running past it hits the guard page. Pointers from elsewhere
// (strdup, SDL, the CRT's own) go to the CRT.
#undef malloc
#undef calloc
#undef realloc
#undef free
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 4096ull
#define RANGE (4ull << 40)  // address space for every block of a run (never reused)
#define MAGIC 0x4755415244484541ull

typedef struct { uint64_t magic, size, base, pages; } Header;  // just before the block

static uint8_t *g_base;
static volatile uint64_t g_next;  // offset of the next free page in the range

static void init(void) {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    BOOL pending;
    if (!InitOnceBeginInitialize(&once, 0, &pending, NULL)) abort();
    if (pending) {
        g_base = VirtualAlloc(NULL, RANGE, MEM_RESERVE, PAGE_NOACCESS);
        if (!g_base) { fprintf(stderr, "guard heap: can't reserve address space\n"); abort(); }
        fprintf(stderr, "[guard heap] blocks at %p (each ends at a guard page; freed pages never reused)\n", g_base);
        InitOnceComplete(&once, 0, NULL);
    }
}

static bool ours(const void *p) { return g_base && (const uint8_t *)p >= g_base && (const uint8_t *)p < g_base + RANGE; }

void *rt_guard_malloc(size_t n) {
    init();
    uint64_t body = (n + 15) & ~15ull;
    uint64_t pages = (body + sizeof(Header) + PAGE - 1) / PAGE;
    uint64_t off = __atomic_fetch_add(&g_next, (pages + 1) * PAGE, __ATOMIC_RELAXED);  // (+1: the guard page)
    if (off + (pages + 1) * PAGE > RANGE) { fprintf(stderr, "guard heap: address space used up\n"); abort(); }
    uint8_t *base = g_base + off;
    if (!VirtualAlloc(base, pages * PAGE, MEM_COMMIT, PAGE_READWRITE)) return NULL;
    uint8_t *p = base + pages * PAGE - body;
    Header *h = (Header *)p - 1;
    *h = (Header){MAGIC, n, (uint64_t)(uintptr_t)base, pages};
    return p;
}

void *rt_guard_calloc(size_t count, size_t n) {
    if (n && count > SIZE_MAX / n) return NULL;
    return rt_guard_malloc(count * n);  // (fresh pages are zero)
}

static Header *header(void *p) {
    Header *h = (Header *)p - 1;
    if (h->magic != MAGIC) {
        fprintf(stderr, "*** guard heap: %p is not a live block (freed twice, or not from malloc)\n", p);
        fflush(stderr);
        abort();
    }
    return h;
}

void rt_guard_free(void *p) {
    if (!p) return;
    if (!ours(p)) { free(p); return; }
    Header *h = header(p);
    h->magic = 0;
    VirtualFree((void *)(uintptr_t)h->base, h->pages * PAGE, MEM_DECOMMIT);  // (stale accesses now fault)
}

void *rt_guard_realloc(void *p, size_t n) {
    if (!p) return rt_guard_malloc(n);
    if (!ours(p)) return realloc(p, n);
    if (!n) { rt_guard_free(p); return NULL; }
    size_t old = header(p)->size;
    void *q = rt_guard_malloc(n);
    if (q) {
        memcpy(q, p, old < n ? old : n);
        rt_guard_free(p);
    }
    return q;
}
