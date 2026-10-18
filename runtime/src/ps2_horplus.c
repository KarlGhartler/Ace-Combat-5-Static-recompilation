/* Hor+ ultrawide: keep the game's vertical field of view and widen the
 * horizontal one to the shape of the picture actually shown.
 *
 * Every 3D view goes through the game's screen-update routine 0x003207F8
 * (a0 = screen struct), the only caller of its view-screen (0x003292B8) and
 * view-clip (0x00329A90) matrix builders.  The struct holds the width W at
 * +0 (640), the height H at +4 (480 in the game's 4:3 mode, 360 in its own
 * 16:9 mode), the focal at +8 and the x scale +0x198 = s16(+0x28) / W, which
 * is recomputed when bit 0 of +0x1A0 is set.  Before the routine runs, W is
 * set to H * picture aspect and the bit is set; after it, the game's W goes
 * back.  The x scale becomes k = (W/H) / picture aspect, the y scale and
 * focal stay, and the clip frustum is built from the same widened W.
 * Nothing else is written -- not the game's 16:9 flag, not the cockpit. */
#include "ps2_runtime.h"
#include "ps2_settings.h"
#include "ps2_hook.h"
#include "ps2_hle.h"
#include "ps2_vk.h"

#include <math.h>
#include <string.h>

#define HP_SCREEN_UPDATE 0x003207F8u

float ps2_horplus_k = 1.0f;     /* x scale of the main 3D view, 1 when off */

#define HP_MAX 32
static struct { u32 s; float w, h, k, set_w; u64 calls; } scr[HP_MAX];
static int nscr;

static float rf(u32 a) { u32 u = ps2_r32(a); float f; memcpy(&f, &u, 4); return f; }
static void wf(u32 a, float f) { u32 u; memcpy(&u, &f, 4); ps2_w32(a, u); }

static int find(u32 s) {
    for (int i = 0; i < nscr; i++) if (scr[i].s == s) return i;
    return -1;
}

#ifndef PS2_HORPLUS_NO_HOOKS  /* gsreplay has no hook layer */
static int hp_before(ps2_ctx *ctx, void *user) {
    u32 s = ps2_arg(ctx, 0);
    float w, h, pa = ps2_vk_display_aspect, design, k = 1.0f;
    int i;
    (void)user;
    if (s < 0x00100000u || s >= 0x01FFFE00u || (s & 3u)) return 0;
    i = find(s);
    w = rf(s);
    h = rf(s + 4u);
    if (i >= 0 && scr[i].set_w != 0.0f && w == scr[i].set_w) w = scr[i].w;  /* left over */
    /* only the game's 640-wide 4:3 / 16:9 screens; skips render targets */
    if (w != 640.0f || (h != 480.0f && h != 360.0f)) return 0;
    if ((s16)ps2_r16(s + 0x28u) != 640) return 0;
    design = w / h;
    if (ps2_cfg.horplus && pa > design * 1.001f && pa < 8.0f) k = design / pa;
    if (i < 0) {
        if (nscr >= HP_MAX) return 0;
        i = nscr++;
        memset(&scr[i], 0, sizeof scr[i]);
        scr[i].s = s; scr[i].k = -1.0f;
        ps2_log("horplus: screen %08X %.0fx%.0f (%s) first used -> x scale %.4f%s", s, w, h,
                h == 480.0f ? "4:3" : "game's 16:9 mode", k,
                ps2_cfg.horplus ? "" : " (Hor+ off)");
    }
    if (scr[i].k >= 0.0f && fabsf(scr[i].k - k) > 1e-4f)
        ps2_log("horplus: screen %08X x scale %.4f -> %.4f (picture %.4f:1)", s, scr[i].k, k, pa);
    if (k < 1.0f || fabsf(scr[i].k - k) > 1e-4f)
        ps2_w32(s + 0x1A0u, ps2_r32(s + 0x1A0u) | 1u);         /* recompute the x scale */
    scr[i].w = w; scr[i].h = h; scr[i].k = k; scr[i].calls++;
    scr[i].set_w = 0.0f;
    if (k < 1.0f) {
        scr[i].set_w = h * pa;
        wf(s, scr[i].set_w);
    }
    ps2_horplus_k = k;
    return 0;
}

static int hp_after(ps2_ctx *ctx, void *user) {
    int ok, i;
    u32 s = ps2_hook_entry_arg(0, &ok);
    (void)user;
    if (!ok) s = ps2_arg(ctx, 0);
    if ((i = find(s)) < 0 || scr[i].set_w == 0.0f) return 0;
    if (rf(s) == scr[i].set_w) wf(s, scr[i].w);
    scr[i].set_w = 0.0f;
    return 0;
}

#endif

void ps2_horplus_init(void) {
#ifndef PS2_HORPLUS_NO_HOOKS
    if (ps2_hook_before(HP_SCREEN_UPDATE, hp_before, NULL, 50, "horplus") < 0
        || ps2_hook_after(HP_SCREEN_UPDATE, hp_after, NULL, 50, "horplus") < 0) {
        ps2_log("horplus: the hook layer refused %08X; Hor+ unavailable", HP_SCREEN_UPDATE);
        return;
    }
    ps2_log("horplus: ready at %08X, Hor+ %s, toggle key scancode %d", HP_SCREEN_UPDATE,
            ps2_cfg.horplus ? "on" : "off", ps2_cfg.horplus_key);
#endif
}

void ps2_horplus_toggle(const char *why) {
    ps2_cfg.horplus = !ps2_cfg.horplus;
    ps2_settings_touch(PS2_CFG_SAVE);
    ps2_log("horplus: Hor+ %s (%s)", ps2_cfg.horplus ? "ON" : "OFF", why);
}

void ps2_horplus_report(void) {
    for (int i = 0; i < nscr; i++)
        ps2_log("horplus: screen %08X %.0fx%.0f updated %llu times, last x scale %.4f",
                scr[i].s, scr[i].w, scr[i].h, (unsigned long long)scr[i].calls, scr[i].k);
}
