/* GWAAN-1: an ambient drone synth for the FM-1, on fm1-base.
 * The app's own files are the author's (FEDLIC TOKYO); fm1-base is MPL-2.0.
 * This header is the app's half of the contract with the platform. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "GWAAN-1"
#define APP_TAGLINE "ambient drone synth"
#define APP_SPLASH_RGB RGB(24, 44, 92)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_DIM_LEVELS                   /* plat_dim_ns: the key LEDs' breathing pulse */

/* the store: the app's settings object and the panel calibration (the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): scene, playing, hold, field,
 * notes, sig, sigpat, sel, k1..k4, dirty, turnsN, leddimn, ledlit, dimns,
 * page, mix1..mix4, mute, arp, arppat, arprate, arpgate, freeze, shim, crush,
 * stage, ledmidn, arpsteps, arpled, ledseenN,
 * snap, snapused, flfo, flfodep, flforate, revecho, wide, dice, midin */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);

/* the sim's reboot/crash/restart commands call this before booting
 * again: the app's RAM state resets, as a real power cycle would
 * (v0.4.1 - without it, a sim "reboot" leaked the runtime state and
 * the persistence tests overstated what a real boot restores) */
#define APP_HOST_REBOOT
void app_host_reboot(void);
