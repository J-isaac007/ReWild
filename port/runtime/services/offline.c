// System services for features this port runs without. Each answers the way a console with nothing nearby would and
// logs what it was asked, so a feature that needs more shows up in the log. After letsgo-recomp's
// runtime/services/offline.c (MIT): csrng is theirs; nfp is BotW's (amiibo).
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ nfp:user (nn::nfp, amiibo)
// IUser (switchbrew "NFC services"): initialized, no NFC device present, so ListDevices finds none and the attach
// events are never signalled. BotW calls nn::nfp::Initialize at boot (its amiibo rune); without an IUser it reads null.
enum { NFP_STATE_NONE = 0, NFP_STATE_INITIALIZED = 1 };

static void nfp_user_iface(Service *s, IpcReq *req, IpcResp *resp) {
    uint32_t *state = s->state;
    switch (req->cmd) {
    case 0: *state = NFP_STATE_INITIALIZED; break;  // Initialize
    case 1: *state = NFP_STATE_NONE; break;         // Finalize
    case 2:                                         // ListDevices(out DeviceHandle[]) -> count
        OUT(uint32_t, 0) = 0;
        resp_size(resp, 4);
        break;
    case 17:  // AttachActivateEvent(device)
    case 18:  // AttachDeactivateEvent(device)
    case 23:  // AttachAvailabilityChangeEvent
        resp->copy_handles[resp->ncopy++] = new_event_handle();
        break;
    case 19:  // GetState
        OUT(uint32_t, 0) = *state;
        resp_size(resp, 4);
        break;
    default: LOG("[nfp] IUser cmd %u (ok, no device)\n", req->cmd);
    }
}

SERVICE("nfp:user", nfp_user) {
    (void)s;
    if (req->cmd == 0) {  // CreateUserInterface
        uint32_t *state = calloc(1, sizeof *state);
        ipc_return_object(resp, service_new("nfp:IUser", nfp_user_iface, state));
        return;
    }
    LOG("[nfp] nfp:user cmd %u (ok)\n", req->cmd);
}

// ------------------------------------------------------------------ csrng (spl random bytes)
SERVICE("csrng", csrng) {
    (void)s;
    if (req->cmd == 0) {  // GenerateRandomBytes(out buffer)
        IpcBuf *b = out_buf(req, 0);
        if (b && b->size) {
            uint8_t *p = (uint8_t *)(uintptr_t)b->addr;
            for (uint64_t i = 0; i < b->size; i += 4) {
                uint32_t v = arc4random();
                memcpy(p + i, &v, b->size - i < 4 ? b->size - i : 4);
            }
        }
        return;
    }
    LOG("[csrng] cmd %u (ok)\n", req->cmd);
}
