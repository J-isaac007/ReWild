// Debug builds only (make GUARD_HEAP=1, Windows): force-included into the runtime's C sources, so their malloc family
// goes to guard_heap.c. Each block gets pages of its own, ending at an inaccessible guard page, and freed pages are
// decommitted and never handed out again: an overrun or a write through a stale pointer faults at the instruction
// that does it (the crash report names it), where the CRT heap only noticed later, if at all (fast fail, no report).
#pragma once
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

void *rt_guard_malloc(size_t n);
void *rt_guard_calloc(size_t count, size_t n);
void *rt_guard_realloc(void *p, size_t n);
void rt_guard_free(void *p);

#define malloc(n) rt_guard_malloc(n)
#define calloc(count, n) rt_guard_calloc(count, n)
#define realloc(p, n) rt_guard_realloc(p, n)
#define free(p) rt_guard_free(p)
