// NVN command buffers. A recorded list's commands are CmdRec records in storage the runtime owns, one list per place
// it's recorded at (control memory position and command buffer, below), not in the game's command memory: BotW's agl
// display lists have several threads recording into the same command memory at once, which corrupted the lists while
// records lived there (LASRecomp's design; reWild research/B2-boot-log.md). Command and control memory are still
// accounted (used / free move a little per command and per list, as the game's offset bookkeeping expects) but never
// written. A command buffer without control memory places its lists by their command memory position instead.
#include <stdlib.h>
#include <string.h>

#include "nvn.h"

#define JUMP_RESERVE (sizeof(CmdRec) + 8)  // room kept at a chunk's end to chain to the next one
#define CMD_MEM_PER_CMD 16                 // command memory a command counts as (a few GPU words on NVN)
#define CTRL_MEM_PER_LIST 32               // control memory a list counts as
#define CHUNK_MIN 0x1000                   // a list's first chunk; each next one is 4x bigger, up to CHUNK_MAX
#define CHUNK_MAX 0x40000

static NvnCmdBuf *cb_of(uint64_t guest) { return HOST(NvnCmdBuf, guest); }

NVN(nvnCommandBufferInitialize) {
    NvnCmdBuf *cb = calloc(1, sizeof *cb);
    cb->guest = A(0);
    SET_HOST(A(0), cb);
    RET(1);
}
NVN(nvnCommandBufferFinalize) { free(cb_of(A(0))); SET_HOST(A(0), 0); }
NVN(nvnCommandBufferSetDebugLabel) { (void)c; }

static CmdRec *raw_rec(uint8_t *at, uint32_t op, uint32_t size) {
    CmdRec *r = (CmdRec *)at;
    memset(r, 0, sizeof *r);
    r->op = op;
    r->size = size;
    return r;
}

// LARECOMP_LOG_CMDBUF=1: command and control memory, recordings and calls
static int log_cmdbuf = -1;
static bool cmdbuf_log(void) {
    if (log_cmdbuf < 0) log_cmdbuf = getenv("LARECOMP_LOG_CMDBUF") != NULL;
    return log_cmdbuf;
}

// ------------------------------------------------------------------ list storage
// A list's records live in a chain of chunks (CMD_JUMP between them). A list belongs to its place: the control memory
// position it was recorded at and the command buffer recording it. Recording at a place that already has a list
// replaces it and reuses its chunks, as NVN reuses the control memory: lists stay bounded by the places a game uses.
// The handle is the runtime's own, one per place, not the control memory address NVN would return: BotW's
// agl::DisplayList hands every display list a slice of one shared ring at its current offset and moves the offset on at
// endDisplayList, so display lists recorded at once on different threads (presumably serialized on the console's
// cooperative per-core scheduling, truly parallel here) start at the same position. Keyed by position alone they shared
// one list and corrupted it (reWild research/B2-boot-log.md).
typedef struct { uint8_t *mem; uint32_t size; } Chunk;
struct NvnList {
    uint64_t handle;
    uint64_t pos, cb;  // its place: control (else command) memory position, guest command buffer
    Chunk *chunks;
    int nchunks, cap;
    int cur;       // write position while recording: chunk, offset
    uint32_t off;
    uint64_t recorder;  // the guest command buffer recording it, 0 once ended
};
// open addressing, one table by handle and one by place
typedef struct { NvnList **slot; uint32_t cap, n; } ListTable;
static ListTable by_handle, by_place;
static uint64_t list_bytes, next_handle = 0x7F00000000ull;  // (handles: outside guest memory, 16 apart)
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t place_key(uint64_t pos, uint64_t cb) { return pos ^ (cb * 0xC2B2AE3D27D4EB4Full); }
static uint32_t slot_of(const ListTable *t, uint64_t k) {
    return (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 32) & (t->cap - 1);
}

static NvnList *find_handle_locked(uint64_t h) {
    if (!by_handle.cap) return NULL;
    for (uint32_t i = slot_of(&by_handle, h);; i = (i + 1) & (by_handle.cap - 1)) {
        NvnList *l = by_handle.slot[i];
        if (!l || l->handle == h) return l;
    }
}

static NvnList *find_place_locked(uint64_t pos, uint64_t cb) {
    if (!by_place.cap) return NULL;
    for (uint32_t i = slot_of(&by_place, place_key(pos, cb));; i = (i + 1) & (by_place.cap - 1)) {
        NvnList *l = by_place.slot[i];
        if (!l || (l->pos == pos && l->cb == cb)) return l;
    }
}

static void insert_locked(ListTable *t, NvnList *l, uint64_t (*key)(const NvnList *)) {
    if ((t->n + 1) * 2 > t->cap) {  // (load factor 1/2)
        NvnList **old = t->slot;
        uint32_t old_cap = t->cap;
        t->cap = t->cap ? t->cap * 2 : 1024;
        t->slot = calloc(t->cap, sizeof *t->slot);
        for (uint32_t i = 0; i < old_cap; i++)
            if (old[i]) {
                uint32_t j = slot_of(t, key(old[i]));
                while (t->slot[j]) j = (j + 1) & (t->cap - 1);
                t->slot[j] = old[i];
            }
        free(old);
    }
    uint32_t j = slot_of(t, key(l));
    while (t->slot[j]) j = (j + 1) & (t->cap - 1);
    t->slot[j] = l;
    t->n++;
}
static uint64_t key_handle(const NvnList *l) { return l->handle; }
static uint64_t key_place(const NvnList *l) { return place_key(l->pos, l->cb); }

static void add_chunk(NvnList *l, int at, uint32_t need) {
    uint32_t size = at == 0 ? CHUNK_MIN : l->chunks[at - 1].size * 4;
    if (size > CHUNK_MAX) size = CHUNK_MAX;
    if (size < need) size = need;
    if (at == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->chunks = realloc(l->chunks, l->cap * sizeof *l->chunks);
    }
    if (at < l->nchunks) {  // (re-recording: a chunk too small for this record is replaced)
        list_bytes -= l->chunks[at].size;
        free(l->chunks[at].mem);
    } else {
        l->nchunks = at + 1;
    }
    l->chunks[at] = (Chunk){malloc(size), size};
    if (!l->chunks[at].mem) FATAL("nvn: out of host memory for command lists\n");
    list_bytes += size;
}

// The list to record at a place: a new one, or the old one emptied for re-recording.
static NvnList *list_for(uint64_t pos, uint64_t cb) {
    pthread_mutex_lock(&list_lock);
    NvnList *l = find_place_locked(pos, cb);
    if (!l) {
        l = calloc(1, sizeof *l);
        l->handle = next_handle += 16;
        l->pos = pos, l->cb = cb;
        add_chunk(l, 0, 0);
        insert_locked(&by_handle, l, key_handle);
        insert_locked(&by_place, l, key_place);
        static uint32_t next_report = 256;
        if (by_handle.n >= next_report) {
            next_report *= 4;
            LOG("[nvn] %u command lists (%.1f MB of records)\n", by_handle.n, list_bytes / 1048576.0);
        }
    }
    l->cur = 0, l->off = 0;
    l->recorder = cb;
    pthread_mutex_unlock(&list_lock);
    return l;
}

static NvnList *list_find(uint64_t handle) {
    pthread_mutex_lock(&list_lock);
    NvnList *l = find_handle_locked(handle);
    pthread_mutex_unlock(&list_lock);
    return l;
}

const CmdRec *nvn_list_first(uint64_t handle) {
    NvnList *l = list_find(handle);
    return l ? (const CmdRec *)l->chunks[0].mem : NULL;
}

// Reboot: the old game's lists go.
RT_ON_RESET(nvn_lists_reset) {
    for (uint32_t i = 0; i < by_handle.cap; i++) {
        NvnList *l = by_handle.slot[i];
        if (!l) continue;
        for (int k = 0; k < l->nchunks; k++) free(l->chunks[k].mem);
        free(l->chunks);
        free(l);
    }
    free(by_handle.slot), free(by_place.slot);
    by_handle = by_place = (ListTable){0};
    list_bytes = 0;
}

// ------------------------------------------------------------------ command and control memory
NVN(nvnCommandBufferAddCommandMemory) {
    NvnCmdBuf *cb = cb_of(A(0));
    uint64_t mem = HOST(NvnPool, A(1))->storage + A(2), size = A(3);
    if (cmdbuf_log())
        LOG("[cmdbuf] %llx add command memory %llx size %llu (recording %d, lr %llx)\n", (unsigned long long)cb->guest,
            (unsigned long long)mem, (unsigned long long)size, cb->recording, (unsigned long long)c->x[30]);
    cb->mem = mem, cb->size = size, cb->used = 0;
}
NVN(nvnCommandBufferAddControlMemory) {
    NvnCmdBuf *cb = cb_of(A(0));
    if (cmdbuf_log())
        LOG("[cmdbuf] %llx add control memory %llx size %llu (recording %d, lr %llx)\n", (unsigned long long)cb->guest,
            (unsigned long long)A(1), (unsigned long long)A(2), cb->recording, (unsigned long long)c->x[30]);
    cb->ctrl = A(1), cb->ctrl_size = A(2), cb->ctrl_used = 0;
}
NVN(nvnCommandBufferGetCommandMemorySize) { RET(cb_of(A(0))->size); }
NVN(nvnCommandBufferGetCommandMemoryUsed) {
    NvnCmdBuf *cb = cb_of(A(0));
    if (cmdbuf_log())
        LOG("[cmdbuf] %llx used? -> %llu (mem %llx, lr %llx)\n", (unsigned long long)cb->guest, (unsigned long long)cb->used,
            (unsigned long long)cb->mem, (unsigned long long)c->x[30]);
    RET(cb->used);
}
NVN(nvnCommandBufferGetCommandMemoryFree) { NvnCmdBuf *cb = cb_of(A(0)); RET(cb->size - cb->used); }
NVN(nvnCommandBufferGetControlMemorySize) { RET(cb_of(A(0))->ctrl_size); }
// (ctrl_used runs on past the size, so each list keeps its own handle; the game sees at most the size)
NVN(nvnCommandBufferGetControlMemoryUsed) {
    NvnCmdBuf *cb = cb_of(A(0));
    RET(cb->ctrl_used < cb->ctrl_size ? cb->ctrl_used : cb->ctrl_size);
}
NVN(nvnCommandBufferGetControlMemoryFree) {
    NvnCmdBuf *cb = cb_of(A(0));
    RET(cb->ctrl_used < cb->ctrl_size ? cb->ctrl_size - cb->ctrl_used : 0);
}
// (the out-of-memory callback is kept for Get*: the runtime never runs out, so it's never called)
NVN(nvnCommandBufferSetMemoryCallback) { cb_of(A(0))->callback = A(1); }
NVN(nvnCommandBufferGetMemoryCallback) { RET(cb_of(A(0))->callback); }
NVN(nvnCommandBufferSetMemoryCallbackData) { cb_of(A(0))->callback_data = A(1); }
NVN(nvnCommandBufferGetMemoryCallbackData) { RET(cb_of(A(0))->callback_data); }
NVN(nvnCommandBufferSetCommandMemoryCallbackEnabled) { (void)c; }

// Reserve a record in the list being recorded. Commands outside a recording go to a scratch record (dropped).
static CmdRec *rec(Cpu *c, NvnCmdBuf *cb, uint32_t op, uint32_t extra) {
    (void)c;
    uint32_t size = (uint32_t)((sizeof(CmdRec) + extra + 7) & ~7u);
    NvnList *l = cb->list;
    if (!l) {
        static __thread uint8_t *scratch;
        static __thread uint32_t scratch_size;
        static int warned;
        if (warned++ < 5) LOG("[nvn] command %u outside a recording on cmdbuf %llx: dropped\n", op, (unsigned long long)cb->guest);
        if (scratch_size < size) free(scratch), scratch = malloc(size), scratch_size = size;
        return raw_rec(scratch, op, size);
    }
    if (l->off + size + JUMP_RESERVE > l->chunks[l->cur].size) {  // chain into the next chunk
        int next = l->cur + 1;
        if (next == l->nchunks || l->chunks[next].size < size + JUMP_RESERVE) {
            pthread_mutex_lock(&list_lock);  // (list_bytes)
            add_chunk(l, next, size + JUMP_RESERVE);
            pthread_mutex_unlock(&list_lock);
        }
        CmdRec *j = raw_rec(l->chunks[l->cur].mem + l->off, CMD_JUMP, sizeof(CmdRec));
        j->x[0] = (uint64_t)(uintptr_t)l->chunks[next].mem;
        l->cur = next, l->off = 0;
    }
    CmdRec *r = raw_rec(l->chunks[l->cur].mem + l->off, op, size);
    l->off += size;
    if (cb->used + CMD_MEM_PER_CMD <= cb->size) cb->used += CMD_MEM_PER_CMD;
    return r;
}

NVN(nvnCommandBufferBeginRecording) {
    NvnCmdBuf *cb = cb_of(A(0));
    cb->recording = true;
    cb->list = list_for(cb->ctrl ? cb->ctrl + cb->ctrl_used : cb->mem + cb->used, cb->guest);
    cb->start = cb->list->handle;
    rec(c, cb, CMD_BARRIER, 0);  // the list starts here
}
NVN(nvnCommandBufferEndRecording) {
    NvnCmdBuf *cb = cb_of(A(0));
    rec(c, cb, CMD_END, 0);
    cb->recording = false;
    if (cb->list) cb->list->recorder = 0;
    cb->list = NULL;
    cb->ctrl_used += CTRL_MEM_PER_LIST;
    if (cmdbuf_log())
        LOG("[cmdbuf] %llx end -> handle %llx (command used %llu of %llu, control used %llu of %llu, lr %llx)\n",
            (unsigned long long)cb->guest, (unsigned long long)cb->start, (unsigned long long)cb->used,
            (unsigned long long)cb->size, (unsigned long long)cb->ctrl_used, (unsigned long long)cb->ctrl_size,
            (unsigned long long)c->x[30]);
    RET(cb->start);
}
NVN(nvnCommandBufferIsRecording) { RET(cb_of(A(0))->recording); }

// ---- generic recorders
typedef struct { uint64_t x[8]; float f[4]; } Snap;
static Snap snap(Cpu *c) {
    Snap s;
    memcpy(s.x, c->x, sizeof s.x);
    for (int i = 0; i < 4; i++) s.f[i] = *(float *)&c->v[i];
    return s;
}
static CmdRec *record(Cpu *c, uint32_t op, uint32_t extra, Snap *s) {
    CmdRec *r = rec(c, cb_of(s->x[0]), op, extra);
    memcpy(r->x, s->x + 1, sizeof r->x);
    memcpy(r->f, s->f, sizeof r->f);
    return r;
}
// record with a copy of `len` bytes from the guest pointer in argument `arg`
static CmdRec *record_copy(Cpu *c, uint32_t op, int arg, uint32_t len) {
    Snap s = snap(c);
    CmdRec *r = record(c, op, len, &s);
    if (s.x[arg] && len) memcpy(r->extra, (void *)(uintptr_t)s.x[arg], len);
    return r;
}
#define PLAIN(name, op) NVN(name) { Snap s = snap(c); record(c, op, 0, &s); }

// ---- state binding
NVN(nvnCommandBufferBindProgram) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_PROGRAM, 0, &s);
    r->x[0] = (uint64_t)(uintptr_t)HOST(NvnProgram, s.x[1]);  // host program
}
NVN(nvnCommandBufferBindBlendState) { record_copy(c, CMD_BIND_BLEND, 1, 8); }
NVN(nvnCommandBufferBindChannelMaskState) { record_copy(c, CMD_BIND_CHANNEL_MASK, 1, 4); }
NVN(nvnCommandBufferBindColorState) { record_copy(c, CMD_BIND_COLOR, 1, 4); }
NVN(nvnCommandBufferBindDepthStencilState) { record_copy(c, CMD_BIND_DEPTH_STENCIL, 1, 8); }
NVN(nvnCommandBufferBindMultisampleState) { record_copy(c, CMD_BIND_MULTISAMPLE, 1, 0x18); }
NVN(nvnCommandBufferBindPolygonState) { record_copy(c, CMD_BIND_POLYGON, 1, 4); }
NVN(nvnCommandBufferBindVertexAttribState) { record_copy(c, CMD_BIND_VERTEX_ATTRIB, 2, (uint32_t)A(1) * 4); }
NVN(nvnCommandBufferBindVertexStreamState) { record_copy(c, CMD_BIND_VERTEX_STREAM, 2, (uint32_t)A(1) * 8); }

// (cmd, first, count, const NVNbufferRange*) and the single-binding forms
NVN(nvnCommandBufferBindVertexBuffers) { record_copy(c, CMD_BIND_VERTEX_BUFFERS, 3, (uint32_t)A(2) * 16); }
NVN(nvnCommandBufferBindVertexBuffer) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_VERTEX_BUFFERS, 16, &s);
    r->x[0] = s.x[1]; r->x[1] = 1;
    memcpy(r->extra, &s.x[2], 16);
}
// (cmd, stage, first, count, const NVNbufferRange*)
NVN(nvnCommandBufferBindUniformBuffers) { record_copy(c, CMD_BIND_UNIFORM_BUFFERS, 4, (uint32_t)A(3) * 16); }
NVN(nvnCommandBufferBindUniformBuffer) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_UNIFORM_BUFFERS, 16, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[2]; r->x[2] = 1;
    memcpy(r->extra, &s.x[3], 16);
}
NVN(nvnCommandBufferBindStorageBuffers) { record_copy(c, CMD_BIND_STORAGE_BUFFERS, 4, (uint32_t)A(3) * 16); }
NVN(nvnCommandBufferBindStorageBuffer) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_STORAGE_BUFFERS, 16, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[2]; r->x[2] = 1;
    memcpy(r->extra, &s.x[3], 16);
}
// (cmd, stage, first, count, const handle*)
NVN(nvnCommandBufferBindTextures) { record_copy(c, CMD_BIND_TEXTURES, 4, (uint32_t)A(3) * 8); }
NVN(nvnCommandBufferBindTexture) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_TEXTURES, 8, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[2]; r->x[2] = 1;
    memcpy(r->extra, &s.x[3], 8);
}
NVN(nvnCommandBufferBindImages) { record_copy(c, CMD_BIND_IMAGES, 4, (uint32_t)A(3) * 8); }
NVN(nvnCommandBufferBindImage) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_IMAGES, 8, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[2]; r->x[2] = 1;
    memcpy(r->extra, &s.x[3], 8);
}
NVN(nvnCommandBufferBindSeparateTextures) { record_copy(c, CMD_BIND_TEXTURES, 4, (uint32_t)A(3) * 8); }
NVN(nvnCommandBufferBindSeparateTexture) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_BIND_TEXTURES, 8, &s);
    r->x[0] = s.x[1]; r->x[1] = s.x[2]; r->x[2] = 1;
    memcpy(r->extra, &s.x[3], 8);
}
NVN(nvnCommandBufferBindSeparateSamplers) { (void)c; }
NVN(nvnCommandBufferBindSeparateSampler) { (void)c; }
NVN(nvnCommandBufferBindTransformFeedbackBuffers) { (void)c; }
NVN(nvnCommandBufferBindTransformFeedbackBuffer) { (void)c; }
NVN(nvnCommandBufferBindCoverageModulationTable) { (void)c; }

NVN(nvnCommandBufferSetTexturePool) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_TEXTURE_POOL, 0, &s);
    r->x[0] = (uint64_t)(uintptr_t)HOST(NvnDescPool, s.x[1]);
}
NVN(nvnCommandBufferSetSamplerPool) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_SAMPLER_POOL, 0, &s);
    r->x[0] = (uint64_t)(uintptr_t)HOST(NvnDescPool, s.x[1]);
}

// SetRenderTargets(cmd, n, colors[], colorViews[], depth, depthView)
// extra: { u64 tex[8]; NvnTextureView views[8]; u8 has_view[8]; u64 depth; NvnTextureView dview; u8 has_dview }
typedef struct {
    uint64_t tex[8];
    NvnTextureView views[8];
    uint8_t has_view[8];
    uint64_t depth;
    NvnTextureView dview;
    uint8_t has_dview;
} RtRec;
NVN(nvnCommandBufferSetRenderTargets) {
    Snap s = snap(c);
    int n = (int)s.x[1];
    if (n > 8) n = 8;
    RtRec rt = {0};
    const uint64_t *colors = (const uint64_t *)(uintptr_t)s.x[2], *views = (const uint64_t *)(uintptr_t)s.x[3];
    for (int i = 0; i < n; i++) {
        rt.tex[i] = colors && colors[i] ? (uint64_t)(uintptr_t)nvn_texture(colors[i], "color target", c->x[30]) : 0;
        if (views && views[i]) { rt.has_view[i] = 1; memcpy(&rt.views[i], (void *)(uintptr_t)views[i], sizeof(NvnTextureView)); }
    }
    rt.depth = s.x[4] ? (uint64_t)(uintptr_t)nvn_texture(s.x[4], "depth target", c->x[30]) : 0;
    if (s.x[5]) { rt.has_dview = 1; memcpy(&rt.dview, (void *)(uintptr_t)s.x[5], sizeof(NvnTextureView)); }
    CmdRec *r = record(c, CMD_SET_RENDER_TARGETS, sizeof rt, &s);
    r->x[0] = (uint64_t)n;
    memcpy(r->extra, &rt, sizeof rt);
}

// viewports/scissors/depth ranges
NVN(nvnCommandBufferSetViewports) { record_copy(c, CMD_SET_VIEWPORTS, 3, (uint32_t)A(2) * 16); }
NVN(nvnCommandBufferSetViewport) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_VIEWPORTS, 16, &s);
    float v[4] = {(float)(int32_t)s.x[1], (float)(int32_t)s.x[2], (float)(int32_t)s.x[3], (float)(int32_t)s.x[4]};
    r->x[0] = 0; r->x[1] = 1;
    memcpy(r->extra, v, 16);
}
NVN(nvnCommandBufferSetScissors) { record_copy(c, CMD_SET_SCISSORS, 3, (uint32_t)A(2) * 16); }
NVN(nvnCommandBufferSetScissor) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_SCISSORS, 16, &s);
    int32_t v[4] = {(int32_t)s.x[1], (int32_t)s.x[2], (int32_t)s.x[3], (int32_t)s.x[4]};
    r->x[0] = 0; r->x[1] = 1;
    memcpy(r->extra, v, 16);
}
NVN(nvnCommandBufferSetDepthRanges) { record_copy(c, CMD_SET_DEPTH_RANGES, 3, (uint32_t)A(2) * 8); }
NVN(nvnCommandBufferSetDepthRange) {
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_SET_DEPTH_RANGES, 8, &s);
    r->x[0] = 0; r->x[1] = 1;
    memcpy(r->extra, s.f, 8);
}
NVN(nvnCommandBufferSetBlendColor) { record_copy(c, CMD_SET_BLEND_COLOR, 1, 16); }
NVN(nvnCommandBufferSetStencilRef) { Snap s = snap(c); CmdRec *r = record(c, CMD_SET_STENCIL, 0, &s); r->x[6] = 0; }
NVN(nvnCommandBufferSetStencilMask) { Snap s = snap(c); CmdRec *r = record(c, CMD_SET_STENCIL, 0, &s); r->x[6] = 1; }
NVN(nvnCommandBufferSetStencilValueMask) { Snap s = snap(c); CmdRec *r = record(c, CMD_SET_STENCIL, 0, &s); r->x[6] = 2; }
PLAIN(nvnCommandBufferSetPrimitiveRestart, CMD_SET_PRIMITIVE_RESTART)
PLAIN(nvnCommandBufferSetAlphaRef, CMD_SET_ALPHA_REF)
PLAIN(nvnCommandBufferSetDepthBounds, CMD_SET_DEPTH_BOUNDS)
PLAIN(nvnCommandBufferSetPolygonOffsetClamp, CMD_SET_POLYGON_OFFSET)
PLAIN(nvnCommandBufferSetLineWidth, CMD_SET_LINE_WIDTH)
PLAIN(nvnCommandBufferSetPointSize, CMD_SET_POINT_SIZE)
PLAIN(nvnCommandBufferSetSampleMask, CMD_SET_SAMPLE_MASK)
PLAIN(nvnCommandBufferSetRenderEnable, CMD_SET_RENDER_ENABLE)
NVN(nvnCommandBufferSetRenderEnableConditional) { (void)c; }
NVN(nvnCommandBufferSetDepthClamp) { (void)c; }
NVN(nvnCommandBufferSetRasterizerDiscard) { (void)c; }
NVN(nvnCommandBufferSetConservativeRasterEnable) { (void)c; }
NVN(nvnCommandBufferSetConservativeRasterDilate) { (void)c; }
NVN(nvnCommandBufferSetSubpixelPrecisionBias) { (void)c; }
NVN(nvnCommandBufferSetViewportSwizzles) { (void)c; }
NVN(nvnCommandBufferSetStencilCullCriteria) { (void)c; }
NVN(nvnCommandBufferSetColorReductionEnable) { (void)c; }
NVN(nvnCommandBufferSetColorReductionThresholds) { (void)c; }
NVN(nvnCommandBufferSetTiledCacheAction) { (void)c; }
NVN(nvnCommandBufferSetTiledCacheTileSize) { (void)c; }
NVN(nvnCommandBufferSetShaderScratchMemory) { (void)c; }
NVN(nvnCommandBufferSetPatchSize) { (void)c; }
NVN(nvnCommandBufferSetInnerTessellationLevels) { (void)c; }
NVN(nvnCommandBufferSetOuterTessellationLevels) { (void)c; }
NVN(nvnCommandBufferSetProgramSubroutines) { (void)c; }
NVN(nvnCommandBufferSaveZCullData) { (void)c; }
NVN(nvnCommandBufferRestoreZCullData) { (void)c; }

static uint64_t copy_row_stride, copy_image_stride;
RT_ON_RESET(cmdbuf_reset) { copy_row_stride = copy_image_stride = 0; }
NVN(nvnCommandBufferSetCopyRowStride) {
    Snap s = snap(c);
    copy_row_stride = s.x[1];
    CmdRec *r = record(c, CMD_SET_COPY_STRIDES, 0, &s);
    r->x[0] = copy_row_stride; r->x[1] = copy_image_stride;
}
NVN(nvnCommandBufferSetCopyImageStride) {
    Snap s = snap(c);
    copy_image_stride = s.x[1];
    CmdRec *r = record(c, CMD_SET_COPY_STRIDES, 0, &s);
    r->x[0] = copy_row_stride; r->x[1] = copy_image_stride;
}
NVN(nvnCommandBufferGetCopyRowStride) { RET(copy_row_stride); }
NVN(nvnCommandBufferGetCopyImageStride) { RET(copy_image_stride); }

// ---- clears
NVN(nvnCommandBufferClearColor) { record_copy(c, CMD_CLEAR_COLOR, 2, 16); }
NVN(nvnCommandBufferClearColori) { CmdRec *r = record_copy(c, CMD_CLEAR_COLOR, 2, 16); r->x[5] = 1; }
NVN(nvnCommandBufferClearColorui) { CmdRec *r = record_copy(c, CMD_CLEAR_COLOR, 2, 16); r->x[5] = 2; }
PLAIN(nvnCommandBufferClearDepthStencil, CMD_CLEAR_DEPTH_STENCIL)
// ClearTexture(cmd, texture, view, region, color, mask)
typedef struct { uint64_t tex; NvnTextureView view; uint8_t has_view; int32_t region[6]; float color[4]; } ClearTexRec;
static void clear_texture(Cpu *c, int kind) {
    Snap s = snap(c);
    ClearTexRec ct = {0};
    ct.tex = s.x[1] ? (uint64_t)(uintptr_t)nvn_texture(s.x[1], "clear texture", c->x[30]) : 0;
    if (s.x[2]) { ct.has_view = 1; memcpy(&ct.view, (void *)(uintptr_t)s.x[2], sizeof ct.view); }
    if (s.x[3]) memcpy(ct.region, (void *)(uintptr_t)s.x[3], sizeof ct.region);
    if (s.x[4]) memcpy(ct.color, (void *)(uintptr_t)s.x[4], 16);
    CmdRec *r = record(c, CMD_CLEAR_TEXTURE, sizeof ct, &s);
    r->x[6] = (uint64_t)kind;
    memcpy(r->extra, &ct, sizeof ct);
}
NVN(nvnCommandBufferClearTexture) { clear_texture(c, 0); }
NVN(nvnCommandBufferClearTexturei) { clear_texture(c, 1); }
NVN(nvnCommandBufferClearTextureui) { clear_texture(c, 2); }
PLAIN(nvnCommandBufferClearBuffer, CMD_CLEAR_BUFFER)
// DiscardColor(cmdbuf, index): the target's contents become undefined. On the Switch a discarded (compressed)
// target reads back cleared; areas the game then leaves undrawn (e.g. a pit after a floor tile falls away) stay
// black there. Treated as a clear to (0,0,0,0) (LARECOMP_KEEP_DISCARD=1: old no-op behaviour).
NVN(nvnCommandBufferDiscardColor) {
    static int keep = -1, logged;
    if (keep < 0) keep = getenv("LARECOMP_KEEP_DISCARD") != NULL;
    if (logged < 5) { logged++; LOG("[nvn] DiscardColor index %llu%s\n", (unsigned long long)c->x[1], keep ? " (kept)" : " -> clear"); }
    if (keep) return;
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_CLEAR_COLOR, 16, &s);
    memset(r->extra, 0, 16);
    r->x[2] = 0xF;  // all channels
    r->x[5] = 0;    // float clear
}
NVN(nvnCommandBufferDiscardDepthStencil) { (void)c; }

// ---- draws (all integer arguments fit in x1..x7)
NVN(nvnCommandBufferDrawArrays) { Snap s = snap(c); CmdRec *r = record(c, CMD_DRAW_ARRAYS, 0, &s); r->x[3] = 0; r->x[4] = 1; }
NVN(nvnCommandBufferDrawArraysInstanced) { Snap s = snap(c); record(c, CMD_DRAW_ARRAYS, 0, &s); }
// Debug: LARECOMP_TRACE_COUNT=<n> logs the guest call chain of indexed draws with n indices.
static void trace_draw(Cpu *c, uint64_t count) {
    static long want = -2;
    if (want == -2) want = getenv("LARECOMP_TRACE_COUNT") ? atol(getenv("LARECOMP_TRACE_COUNT")) : -1;
    if (want < 0 || (long)count != want) return;
    static int n;
    if (n++ > 40) return;
    LOG("[trace] draw count %llu lr %llx chain:", (unsigned long long)count, (unsigned long long)c->x[30]);
    uint64_t fp = c->x[29];
    for (int i = 0; i < 14 && fp >= 0x7000000000ull && fp < 0x8000000000ull && (fp & 7) == 0; i++) {
        uint64_t next = *(uint64_t *)(uintptr_t)fp, lr = *(uint64_t *)(uintptr_t)(fp + 8);
        LOG(" %llx", (unsigned long long)lr);
        if (next <= fp) break;
        fp = next;
    }
    LOG("\n");
}

NVN(nvnCommandBufferDrawElements) {
    trace_draw(c, c->x[3]);
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_DRAW_ELEMENTS, 0, &s);
    r->x[4] = 0; r->x[5] = 0; r->x[6] = 1;  // base vertex, base instance, instances
}
NVN(nvnCommandBufferDrawElementsBaseVertex) {
    trace_draw(c, c->x[3]);
    Snap s = snap(c);
    CmdRec *r = record(c, CMD_DRAW_ELEMENTS, 0, &s);
    r->x[5] = 0; r->x[6] = 1;
}
NVN(nvnCommandBufferDrawElementsInstanced) { trace_draw(c, c->x[3]); Snap s = snap(c); record(c, CMD_DRAW_ELEMENTS, 0, &s); }
PLAIN(nvnCommandBufferDrawArraysIndirect, CMD_DRAW_ARRAYS_INDIRECT)
PLAIN(nvnCommandBufferDrawElementsIndirect, CMD_DRAW_ELEMENTS_INDIRECT)
NVN(nvnCommandBufferMultiDrawArraysIndirectCount) { (void)c; }
NVN(nvnCommandBufferMultiDrawElementsIndirectCount) { (void)c; }
NVN(nvnCommandBufferDrawTransformFeedback) { (void)c; }
NVN(nvnCommandBufferDrawTexture) { (void)c; }
PLAIN(nvnCommandBufferDispatchCompute, CMD_DISPATCH)
PLAIN(nvnCommandBufferDispatchComputeIndirect, CMD_DISPATCH_INDIRECT)
NVN(nvnCommandBufferBeginTransformFeedback) { (void)c; }
NVN(nvnCommandBufferEndTransformFeedback) { (void)c; }
NVN(nvnCommandBufferPauseTransformFeedback) { (void)c; }
NVN(nvnCommandBufferResumeTransformFeedback) { (void)c; }

// ---- copies
typedef struct { uint64_t tex; NvnTextureView view; uint8_t has_view; int32_t region[6]; } TexRef;
static void tex_ref(TexRef *t, uint64_t tex, uint64_t view, uint64_t region) {
    t->tex = tex ? (uint64_t)(uintptr_t)nvn_texture(tex, "copy", 0) : 0;
    if (view) { t->has_view = 1; memcpy(&t->view, (void *)(uintptr_t)view, sizeof t->view); }
    if (region) memcpy(t->region, (void *)(uintptr_t)region, sizeof t->region);
}
NVN(nvnCommandBufferCopyBufferToTexture) {  // (cmd, src, dst, dstView, dstRegion, flags)
    Snap s = snap(c);
    TexRef t = {0};
    tex_ref(&t, s.x[2], s.x[3], s.x[4]);
    CmdRec *r = record(c, CMD_COPY_BUFFER_TO_TEXTURE, sizeof t, &s);
    memcpy(r->extra, &t, sizeof t);
}
NVN(nvnCommandBufferCopyTextureToBuffer) {  // (cmd, src, srcView, srcRegion, dst, flags)
    Snap s = snap(c);
    TexRef t = {0};
    tex_ref(&t, s.x[1], s.x[2], s.x[3]);
    CmdRec *r = record(c, CMD_COPY_TEXTURE_TO_BUFFER, sizeof t, &s);
    memcpy(r->extra, &t, sizeof t);
}
NVN(nvnCommandBufferCopyTextureToTexture) {  // (cmd, src, srcView, srcRegion, dst, dstView, dstRegion, flags)
    Snap s = snap(c);
    TexRef t[2] = {{0}};
    tex_ref(&t[0], s.x[1], s.x[2], s.x[3]);
    tex_ref(&t[1], s.x[4], s.x[5], s.x[6]);
    CmdRec *r = record(c, CMD_COPY_TEXTURE_TO_TEXTURE, sizeof t, &s);
    memcpy(r->extra, t, sizeof t);
}
PLAIN(nvnCommandBufferCopyBufferToBuffer, CMD_COPY_BUFFER_TO_BUFFER)
NVN(nvnCommandBufferDownsample) {
    Snap s = snap(c);
    TexRef t[2] = {{0}};
    tex_ref(&t[0], s.x[1], 0, 0);
    tex_ref(&t[1], s.x[2], 0, 0);
    CmdRec *r = record(c, CMD_DOWNSAMPLE, sizeof t, &s);
    memcpy(r->extra, t, sizeof t);
}
NVN(nvnCommandBufferDownsampleTextureView) {
    Snap s = snap(c);
    TexRef t[2] = {{0}};
    tex_ref(&t[0], s.x[1], s.x[2], 0);
    tex_ref(&t[1], s.x[3], s.x[4], 0);
    CmdRec *r = record(c, CMD_DOWNSAMPLE, sizeof t, &s);
    memcpy(r->extra, t, sizeof t);
}
NVN(nvnCommandBufferTiledDownsample) { (void)c; }
NVN(nvnCommandBufferTiledDownsampleTextureView) { (void)c; }
NVN(nvnCommandBufferResolveDepthBuffer) { (void)c; }
// UpdateUniformBuffer(cmd, buffer, size, offset, dataSize, data)
NVN(nvnCommandBufferUpdateUniformBuffer) { record_copy(c, CMD_UPDATE_UNIFORM_BUFFER, 5, (uint32_t)A(4)); }

// ---- synchronization and misc
NVN(nvnCommandBufferFenceSync) {
    // work executes at submit time, so the sync is satisfied once this list has run
    Snap s = snap(c);
    record(c, CMD_FENCE_SYNC, 0, &s);
}
PLAIN(nvnCommandBufferWaitSync, CMD_WAIT_SYNC)
PLAIN(nvnCommandBufferBarrier, CMD_BARRIER)
PLAIN(nvnCommandBufferSignalEvent, CMD_SIGNAL_EVENT)
PLAIN(nvnCommandBufferWaitEvent, CMD_WAIT_EVENT)
PLAIN(nvnCommandBufferReportCounter, CMD_REPORT_COUNTER)
NVN(nvnCommandBufferReportValue) { (void)c; }
NVN(nvnCommandBufferResetCounter) { (void)c; }
// Call / copy a list into the one being recorded. NVN copies the called list's entries into this command buffer when
// the call is recorded, so the game may re-record the called list right after (agl re-records its display lists on
// worker threads every frame). Resolving the handle later, at execution (LASRecomp's CMD_CALL), raced with that. The
// called list's records are copied here instead (its own calls were inlined when it was recorded).
static void inline_lists(Cpu *c, const char *what) {
    NvnCmdBuf *cb = cb_of(A(0));
    uint64_t count = A(1), handles = A(2);
    if (cmdbuf_log())
        LOG("[cmdbuf] %llx %s %llu lists: %llx%s (lr %llx)\n", (unsigned long long)cb->guest, what, (unsigned long long)count,
            (unsigned long long)(count ? *(const uint64_t *)(uintptr_t)handles : 0), count > 1 ? " ..." : "",
            (unsigned long long)c->x[30]);
    for (uint64_t i = 0; i < count; i++) {
        uint64_t h = ((const uint64_t *)(uintptr_t)handles)[i];
        NvnList *l = list_find(h);
        if (!l) {
            static int warned;
            if (warned++ < 10) LOG("[nvn] %s of handle %llx: no such list\n", what, (unsigned long long)h);
            continue;
        }
        if (l->recorder) {
            static int warned;
            if (warned++ < 10)
                LOG("[nvn] cmdbuf %llx: %s of handle %llx, which cmdbuf %llx is still recording (lr %llx)\n",
                    (unsigned long long)cb->guest, what, (unsigned long long)h, (unsigned long long)l->recorder,
                    (unsigned long long)c->x[30]);
        }
        const CmdRec *r = (const CmdRec *)l->chunks[0].mem;
        for (int guard = 0; guard < 10000000 && r->op != CMD_END; guard++) {
            if (r->op == CMD_JUMP) { r = (const CmdRec *)(uintptr_t)r->x[0]; continue; }
            CmdRec *d = rec(c, cb, r->op, r->size - (uint32_t)sizeof(CmdRec));
            memcpy(d, r, r->size);
            r = (const CmdRec *)((const uint8_t *)r + r->size);
        }
    }
}
NVN(nvnCommandBufferCallCommands) { inline_lists(c, "call"); }
NVN(nvnCommandBufferCopyCommands) { inline_lists(c, "copy"); }
NVN(nvnCommandBufferInsertDebugMarker) { (void)c; }
NVN(nvnCommandBufferInsertDebugMarkerDynamic) { (void)c; }
NVN(nvnCommandBufferInsertDebugMarkerStatic) { (void)c; }
NVN(nvnCommandBufferPushDebugGroup) { (void)c; }
NVN(nvnCommandBufferPushDebugGroupDynamic) { (void)c; }
NVN(nvnCommandBufferPushDebugGroupStatic) { (void)c; }
NVN(nvnCommandBufferPopDebugGroup) { (void)c; }
NVN(nvnCommandBufferPopDebugGroupId) { (void)c; }
