/* GWAAN-1: an ambient drone synth (spec v0.1, v0.2 additions, v0.3 live
 * edition, v0.4). The app's own code (FEDLIC TOKYO); the platform is
 * fm1-base (MPL-2.0). Part of the unity build: gfx.c and announce.c are
 * in scope.
 *
 * v0.3 (live performance): a MIXER page (EDIT) with per-layer volumes and
 * mutes, an FX page (the FX button) with SHIMMER (octave-up reverb send)
 * and CRUSH (bit/rate reduction), an arpeggiator for the keyboard voice
 * (ARP button: tap on/off, hold for pattern), FREEZE (the GLO button:
 * the drone wash captured into a held loop), live key lights (arp steps,
 * mixer layer meters, the freeze glow) and a STAGE LED brightness mode
 * (HOME held). The normal page's controls are exactly v0.2.1's.
 *
 * v0.4: SNAPSHOTS (4 slots: SEQ cycles, SEQ held stores, HOME tap
 * recalls - with a 1.2 s crossfade glide over every continuous
 * parameter), a filter LFO (the LFO button; depth/rate on FX page B),
 * DICE (the SEL button on the normal page: a safe-range re-roll through
 * the same glide), REVERSE ECHO and STEREO WIDE (FX page B), and USB
 * MIDI: scale-snapped note input plus CC control of the main parameters.
 *
 * Layers, generated from power-on:
 *   MIHYON - an FM voice whose modulator pitch glides slowly (seconds-long
 *            cycles), so the harmonics sweep and wail, gently.
 *   GUWAAN - a low sub drone plus a chord field that crossfades every
 *            35-50 s among soft sus/add9 voicings, swelling as it goes.
 *            v0.2: triple-detuned pads, a sub 2nd harmonic, wider swells
 *            and a root-x4 body oscillator, all under KNOB2 (fatness).
 *   AIR    - filtered noise (wind / water) and rare water-drop bells.
 *   SIGNAL - (v0.2) sparse, distant beacon pings: soft sine "transmitter"
 *            tones in short morse-like patterns, pitched from the field,
 *            washed in space. KNOB4's upper travel brings them in.
 * The panel is four wide-sweet-spot knob macros (glide / depth / space /
 * drift) plus POKE-1 v0.1.1's SELECT main-filter curve. Everything is
 * smoothed, and a soft saturator + limiter keep the ceiling: the gentle-
 * stimulation design is a hard requirement - it never turns harsh. */
#include "fastmath.h"

#define SRATE 44100.0f
#define TWO_PI 6.2831853f

/* ----------------------------------------------------------------- scenes --- */
typedef struct {
    const char *name;
    uint16_t col_top, col_bot;       /* the screen gradient */
    float root;                      /* the field root, Hz (the sub lives here) */
    uint8_t nsc;
    int8_t scale[7];                 /* semitones: the keyboard's scale lock */
    int8_t field[4][4];              /* voicings {sub, pad, pad, pad}, semitone offsets */
    float field_s;                   /* seconds per chord field */
    float ratio;                     /* the mihyon voice's FM modulator ratio */
    float air;                       /* the air layer's amount, 0..1 */
    float drop;                      /* water-drop probability multiplier */
    float rev;                       /* the reverb send scale */
    float sig;                       /* SIGNAL density multiplier (pattern rate) */
    float sig_top;                   /* the beacon band's ceiling, Hz */
} scene_t;

enum { SC_DEEP, SC_FOREST, SC_RAIN, SC_STARS, SC_DAWN, NSCENE };
static const scene_t SCENE[NSCENE] = {
    /* DEEP: D minor pentatonic, the lowest root */
    {"DEEP", RGB(6, 16, 38), RGB(18, 56, 92), 73.42f, 5, {0, 3, 5, 7, 10, 0, 0},
     {{0, 12, 19, 27}, {0, 12, 19, 26}, {3, 15, 22, 26}, {5, 17, 24, 31}},
     40.0f, 2.00f, 0.55f, 0.5f, 1.0f, 0.8f, 700.0f},
    /* FOREST: G major pentatonic */
    {"FOREST", RGB(10, 34, 20), RGB(40, 88, 46), 98.00f, 5, {0, 2, 4, 7, 9, 0, 0},
     {{0, 12, 19, 26}, {4, 16, 23, 28}, {2, 14, 21, 26}, {7, 19, 26, 31}},
     45.0f, 3.01f, 0.45f, 0.7f, 0.9f, 0.5f, 1500.0f},
    /* RAIN: A minor pentatonic, the most drops, almost no signal */
    {"RAIN", RGB(18, 24, 34), RGB(54, 74, 98), 110.00f, 5, {0, 3, 5, 7, 10, 0, 0},
     {{0, 12, 19, 24}, {0, 12, 22, 27}, {3, 15, 22, 29}, {5, 17, 24, 29}},
     35.0f, 1.50f, 0.75f, 1.6f, 1.1f, 0.12f, 1200.0f},
    /* STARS: C lydian, the shimmer scene, the most signal-like */
    {"STARS", RGB(4, 6, 26), RGB(32, 28, 68), 130.81f, 7, {0, 2, 4, 6, 7, 9, 11},
     {{0, 12, 19, 26}, {0, 12, 23, 30}, {7, 19, 26, 33}, {2, 14, 26, 33}},
     50.0f, 4.16f, 0.35f, 0.3f, 1.15f, 1.3f, 1900.0f},
    /* DAWN: F major pentatonic, warm */
    {"DAWN", RGB(40, 26, 42), RGB(150, 86, 54), 87.31f, 5, {0, 2, 4, 7, 9, 0, 0},
     {{0, 12, 19, 24}, {0, 12, 21, 26}, {5, 17, 24, 31}, {7, 19, 26, 31}},
     38.0f, 2.76f, 0.40f, 0.5f, 0.95f, 0.6f, 1600.0f},
};

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x47574131u            /* "GWA1" */
/* A snapshot: the full settings set, for live recall (v0.4). */
typedef struct {
    uint8_t scene, mute, feat3;          /* feat3: feat's arp bits (on + pattern) */
    int8_t sel;
    uint8_t k1, k2, k3, k4;
    uint8_t mix[4];
    uint8_t arp_rate, arp_gate, shimmer, crush;
    uint8_t revecho, wide;
    uint8_t flfo, flfo_dep, flfo_rate;
    uint8_t freeze;
} snap_t;                                /* 22 bytes */

/* The first 16 bytes are v0.1..v0.2.1's layout, byte-identical; the v0.3
 * and v0.4 blocks are appended and loaded in stages (POKE-1's scheme):
 * an older blob loads with the newer blocks at neutral defaults. */
typedef struct {
    uint32_t magic;
    uint8_t scene, playing, rsv0, rsv1;
    int8_t sel;                          /* SELECT detents, -48..48 */
    uint8_t k1, k2, k3, k4;              /* the knob macros, 0..127 */
    /* ---- v0.3 extension (bytes 16..27) ---- */
    uint8_t mix[4];                      /* layer volumes: MIHYON GUWAAN AIR SIGNAL */
    uint8_t mute;                        /* bit i: layer i muted */
    uint8_t feat;                        /* bit0 arp on, bits1-2 arp pattern, bit3 stage LEDs */
    uint8_t arp_rate, arp_gate;          /* 0..127 */
    uint8_t shimmer, crush;              /* the FX page's amounts, 0..127 */
    uint8_t rsv[2];
    /* ---- v0.4 extension (bytes 28..124) ---- */
    uint8_t revecho, wide;               /* the FX-B page's amounts, 0..127 */
    uint8_t flfo;                        /* bit0: the filter LFO is on */
    uint8_t flfo_dep, flfo_rate;         /* 0..127 */
    uint8_t snap_sel, snap_used;         /* the selected slot 0..3; bitmask of stored slots */
    snap_t snap[4];
    uint8_t rsv4[2];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static void settings_ext_defaults(void)  /* the v0.3 block, neutral */
{
    int i;
    for (i = 0; i < 4; i++)
        set.mix[i] = 127;                /* unity: the v0.2.1 sound, untouched */
    set.mute = 0;
    set.feat = 0;                        /* arp off, pattern UP, normal LEDs */
    set.arp_rate = 40;                   /* ~0.68 s steps */
    set.arp_gate = 96;                   /* ~79% gate */
    set.shimmer = 0;
    set.crush = 0;
    set.rsv[0] = set.rsv[1] = 0;
}

static void settings_ext4_defaults(void) /* the v0.4 block, neutral */
{
    set.revecho = 0;
    set.wide = 0;                        /* mono: the v0.3 sound, bit for bit */
    set.flfo = 0;
    set.flfo_dep = 40;
    set.flfo_rate = 64;                  /* a ~9 s period */
    set.snap_sel = 0;
    set.snap_used = 0;
    memset(set.snap, 0, sizeof set.snap);
    set.rsv4[0] = set.rsv4[1] = 0;
}

static void settings_defaults(void)
{
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.scene = SC_DEEP;
    set.playing = 1;
    set.sel = 0;
    set.k1 = 64;                         /* glide */
    set.k2 = 64;                         /* depth */
    set.k3 = 70;                         /* space */
    set.k4 = 40;                         /* drift */
    settings_ext_defaults();
    settings_ext4_defaults();
}
static void settings_load(void)
{
    int32_t n;
    settings_defaults();
    n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    if (n < 16 || set.magic != SET_MAGIC || set.scene >= NSCENE || set.playing > 1u ||
        set.sel < -48 || set.sel > 48 || set.k1 > 127u || set.k2 > 127u ||
        set.k3 > 127u || set.k4 > 127u) {
        settings_defaults();
        return;
    }
    if (n < 28 || set.mute > 15u || set.feat > 15u || set.arp_rate > 127u ||
        set.arp_gate > 127u || set.shimmer > 127u || set.crush > 127u)
        settings_ext_defaults();         /* a v0.2.x blob, or a corrupt v0.3 block */
    if (n < (int32_t)sizeof set || set.revecho > 127u || set.wide > 127u ||
        set.flfo > 1u || set.flfo_dep > 127u || set.flfo_rate > 127u ||
        set.snap_sel > 3u || set.snap_used > 15u)
        settings_ext4_defaults();        /* a v0.3 blob, or a corrupt v0.4 block */
}

/* ------------------------------------------------------------------- RNG ---- */
static uint32_t rng_s = 0x1F123BB5u;
static uint32_t rng_next(void)               /* xorshift32: cheap, no libc rand in the render */
{
    uint32_t x = rng_s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_s = x;
    return x;
}
static float rng_f(void) { return (float)(rng_next() >> 8) * (1.0f / 16777216.0f); }

/* ------------------------------------------------------- smoothed macros ---- */
/* Written by params_from_panel() at frame rate; the render glides to them. */
static struct {
    float sel;                           /* -1..1, the SELECT filter character */
    float glide_per, glide_dep;          /* K1: the mihyon glide's period (s) and depth (octaves of ratio) */
    float sub_w, swell_s;                /* K2: sub weight, the swell LFO's period (s) */
    float body, detune, swell_dep;       /* K2 (v0.2): mid-low body weight, pad detune spread, swell depth */
    float echo_send, echo_fb, rev_send;  /* K3: the space wash */
    float drift, shimmer;                /* K4: random drift amount, high sparkle amount */
    float sig_amt;                       /* K4 upper travel (v0.2): the SIGNAL layer's amount, 0..1 */
    float mix[4];                        /* v0.3: the mixer page's layer gains (mute folds to 0) */
    float shim_fx, crush;                /* v0.3: the FX page's SHIMMER and CRUSH amounts, 0..1 */
    float revecho, wide;                 /* v0.4: the FX-B page's amounts, 0..1 */
    float flfo_per, flfo_amp;            /* v0.4: the filter LFO's period (s) and depth (sel units) */
} TGT;
static struct {                          /* the render's own smoothed copies */
    float sel, lim;
    float drift_cut, drift_det, drift_gld;
    float body, detune;                  /* K2's fatness, glided so turns never step */
    float mix[4];                        /* v0.3: glided layer gains - mutes never click */
    float shim, crush, frz;              /* v0.3: glided FX amounts; frz is the freeze blend */
    float revecho, wide;                 /* v0.4: glided FX-B amounts */
} SM;

/* v0.4: the snapshot recall glide's state (the machinery is with the UI;
 * declared here because the knob-answer logic must know a glide is not
 * a knob turn) */
enum { RC_SEL, RC_K1, RC_K2, RC_K3, RC_K4, RC_MIX0, RC_MIX1, RC_MIX2, RC_MIX3,
       RC_SHIM, RC_CRUSH, RC_REV, RC_WIDE, RC_FDEP, RC_FRATE, RC_ARATE, RC_AGATE, RC_N };
static struct {
    uint8_t on;
    uint32_t t0;
    float from[RC_N], to[RC_N];
} rc;

/* ------------------------------------------------------------- the field ---- */
/* Two slots crossfade: each holds a voicing (4 frequencies) and a gain that
 * glides to its target with a ~3.3 s time constant (a ~10 s crossfade). */
static struct {
    float freq[4];                       /* sub, pad, pad, pad */
    float ph[4][3];                      /* phases: pads run detuned triples (v0.2);
                                          * the sub row also carries its fifth and 2nd harmonic */
    float g, tgt;
    int voice;                           /* the voicing index it holds */
} slot[2];
static int slot_cur;                     /* the slot fading in / dominant */
static uint32_t field_left;              /* samples until the next transition */
static volatile uint8_t eng_hold;        /* REC long-press: the field is held */
static float sub_freq;                   /* the sub's frequency, glided on scene change */
static float swell_ph;                   /* the GUWAAN swell LFO's phase */

static void slot_assign(int s, int voice)
{
    const scene_t *sc = &SCENE[set.scene];
    int i;
    slot[s].voice = voice;
    for (i = 0; i < 4; i++)
        slot[s].freq[i] = sc->root * fm_exp2f((float)sc->field[voice][i] / 12.0f);
}
static void field_advance(void)
{
    int old = slot_cur, nw = 1 - slot_cur;
    slot_assign(nw, (slot[old].voice + 1) & 3);
    slot[nw].tgt = 1.0f;
    slot[old].tgt = 0.0f;
    slot_cur = nw;
    field_left = (uint32_t)(SCENE[set.scene].field_s * SRATE);
}

/* ----------------------------------------------------------------- voices --- */
/* MIHYON: an FM voice whose modulator ratio glides in slow cycles. */
#define NMIH 3
typedef struct {
    uint8_t on;
    float ph, mph, freq, ratio;
    float gph;                           /* the glide cycle's phase */
    float peak;
    uint32_t age, atk, dur;
    float ans_t;                         /* v0.2.1: the KNOB1 answer sweep: -1 idle, else 0..1 */
    int8_t ans_dir;                      /* its direction: the way the knob was turned */
} mih_t;
static mih_t mih[NMIH];

/* the keyboard: scale-locked FM notes with a long, melting release */
#define NKEYV 4
typedef struct {
    uint8_t st;                          /* 0 off, 1 held, 2 releasing */
    int8_t key;                          /* the owner: a panel key (0..26) or a MIDI note (-1-note) */
    float ph, mph, freq, level;
    float vel;                           /* v0.4: the note's velocity (MIDI); panel notes use 1 */
    uint32_t rel;                        /* samples since release began */
} keyv_t;
static keyv_t keyv[NKEYV];
static volatile uint32_t keys_down;      /* the panel keys, from the UI */

/* water drops (and, at high pitch, the K4 shimmer pings) */
#define NDROP 3
typedef struct {
    uint8_t on;
    float ph, f, freq, level;
    uint32_t age;
} drop_t;
static drop_t drop[NDROP];

/* SIGNAL (v0.2): distant beacon pings - a soft sine with a smooth attack and
 * a long decay, fired in short morse-like patterns, then long silence. Two
 * voices so a new ping can start while the previous tail is still fading. */
#define NSIG 2
typedef struct {
    uint8_t on;
    float ph, freq, peak, tau, wob_ph;
    uint32_t age, atk;
} sig_t;
static sig_t sigv[NSIG];
static uint32_t next_sig_at;                 /* the next pattern decision point */
static uint32_t next_ping_at;                /* the next ping inside a pattern */
static int sig_left;                         /* pings left in the current pattern */
static float sig_pat_freq;                   /* the pattern's home pitch */
static volatile uint32_t sig_pings, sig_pats;    /* the tests read these */
static volatile uint32_t sig_last_at;        /* when the last ping started (samples) */
static volatile float sig_last_freq;         /* its pitch (the UI shows/lights it) */
static float body_ph, body3_ph;              /* the GUWAAN body oscillators (root x4, root x3) */
static float body2_ph;                       /* v0.2.1: root x2 - the low-mid warmth oscillator */

/* the air layer's filter state */
static float air_lp, air_hp, air_am_ph;
static uint32_t nz_s = 0x85EBCA6Bu;
static float noise_f(void)               /* a second, independent xorshift for the air */
{
    uint32_t x = nz_s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    nz_s = x;
    return (float)(x >> 9) * (1.0f / 8388608.0f) - 1.0f;
}

/* -------------------------------------------------------------- the space --- */
/* A long tape echo and a small Schroeder reverb, both in the pool: together
 * ~97 KB, more than the whole 96 KB RAM region could hold beside .bss. */
#define ECHO_LEN 24255                   /* 0.55 s at 44.1 kHz */
static float echo_buf[ECHO_LEN] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t echo_pos;
static float echo_lp;
static float comb_buf[4][1700] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t comb_pos[4];
static const uint16_t COMB_LEN[4] = {1557, 1617, 1491, 1422};
static float ap_buf[2][560] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t ap_pos[2];
static const uint16_t AP_LEN[2] = {556, 441};

/* FREEZE (v0.3): the layer mix is recorded into a 0.7 s ring at all times;
 * engaging FREEZE stops the recording and loops the ring through two
 * Hann-windowed heads half a loop apart (their gains sum to 1, so the
 * loop seam is never heard). The blend (SM.frz) glides, so capture and
 * release are crossfades, never cuts. int16 in the pool: 61,740 B. */
#define FRZ_LEN 30870                    /* 0.7 s at 44.1 kHz */
static int16_t frz_buf[FRZ_LEN] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t frz_pos;                 /* the record head */
static float frz_p;                      /* the playback phase, in samples */
static volatile uint8_t eng_freeze;      /* the UI's FREEZE switch, read by the render */

/* SHIMMER (v0.3): a granular octave-up send into the reverb. The post-filter
 * dry signal fills a 4096-sample line; two read heads run at double speed
 * (delay shrinking from WIN to 0), Hann-windowed half a window apart, so
 * the shifted copy is continuous. 16,384 B in the pool. */
#define SHIM_LEN 4096
#define SHIM_WIN 2048
static float shim_buf[SHIM_LEN] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t shim_pos;
static float shim_ph;                    /* the grain phase, 0..1 */

/* CRUSH (v0.3): sample-hold + bit reduction on the dry mix, before the
 * filter (so SELECT can still sweep the crushed harmonics). The wet mix
 * is SM.crush itself, which glides: engaging never steps. */
static float crush_val;
static uint32_t crush_cnt;

/* REVERSE ECHO (v0.4): the post-filter signal is recorded into a ring;
 * two Hann-windowed grains play the last ~0.5 s backwards into the wet
 * path, so echoes bloom in reverse. int16 in the pool: 48,510 B. */
#define REV_LEN 24255                    /* 0.55 s at 44.1 kHz */
#define REV_GRAIN 22207.0f               /* REV_LEN - 2048, as a float */
static int16_t rev_buf[REV_LEN] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t rev_pos;
static float rev_age;                    /* the grain age, in samples */

/* STEREO WIDE (v0.4): the right channel's decorrelator - a short ring
 * whose read delay grows with the width (0 at width 0, so the mono
 * render is untouched). The second echo tap and the reverb's two
 * allpass taps are the other width sources. */
static float dec_buf[32];
static uint32_t dec_pos;
static float echo_lp2;                   /* the echo's second tap (half the delay) */

/* per-layer activity envelopes (post-mixer contributions), for the mixer
 * page's key-light meters; written by the render, read by the UI */
static volatile float eng_lvl[4];

/* SELECT (v0.2.1): a real resonant filter, replacing the POKE-1-derived
 * tone tilt the user called out on the device ("tone, not a filter").
 * Two Zavalishin (TPT) state-variable stages in cascade per side give a
 * 4-pole (24 dB/oct) slope with a genuine resonance peak at the cutoff.
 * The layout is the DJ one: the centre is fully open, the lowpass sweeps
 * down to the left (16 kHz -> 120 Hz), the highpass up to the right
 * (25 Hz -> 5 kHz). Both cascades process the input at all times, so
 * crossing the centre stays clickless. Damping k = 0.65 (Q ~ 1.5 per
 * stage, ~3 combined): squelchy, well short of self-oscillation, and
 * the output limiter still guards the ceiling. */
static float flt_lp_ic[2][2], flt_hp_ic[2][2];
static float svf_stage(float x, float g, float k, float *ic, int high)
{
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1, a3 = g * a2;
    float v3 = x - ic[1];
    float v1 = a1 * ic[0] + a2 * v3;
    float v2 = ic[1] + a2 * ic[0] + a3 * v3;
    ic[0] = 2.0f * v1 - ic[0];
    ic[1] = 2.0f * v2 - ic[1];
    return high ? x - k * v1 - v2 : v2;
}
static float main_filter(float x, float sel, float cut_mul)
{
    float t, fc, g, lp, hp;
    /* the lowpass cascade */
    t = sel < -0.02f ? (-sel - 0.02f) * 1.0204f : 0.0f;
    fc = 16000.0f * fm_exp2f(-7.06f * t) * cut_mul;
    if (fc > 16000.0f)
        fc = 16000.0f;
    g = fm_tanf(3.14159265f * fc / SRATE);
    lp = svf_stage(x, g, 0.65f, flt_lp_ic[0], 0);
    lp = svf_stage(lp, g, 0.65f, flt_lp_ic[1], 0);
    /* the highpass cascade */
    t = sel > 0.02f ? (sel - 0.02f) * 1.0204f : 0.0f;
    fc = 25.0f * fm_exp2f(7.64f * t) * cut_mul;
    if (fc < 12.0f)
        fc = 12.0f;
    g = fm_tanf(3.14159265f * fc / SRATE);
    hp = svf_stage(x, g, 0.65f, flt_hp_ic[0], 1);
    hp = svf_stage(hp, g, 0.65f, flt_hp_ic[1], 1);
    return sel < -0.02f ? lp : sel > 0.02f ? hp : x;
}

/* -------------------------------------------------------------- scheduler --- */
static uint64_t sample_now;
static uint32_t next_mih_at, next_drop_at, next_drift_at;
static uint32_t gen_notes;                   /* mihyon + drop notes since boot (the tests read it) */
static volatile uint8_t eng_playing = 1;

static void mih_start(void)
{
    const scene_t *sc = &SCENE[set.scene];
    int i, cand[5];
    mih_t *m = 0;
    for (i = 0; i < NMIH; i++)
        if (!mih[i].on) {
            m = &mih[i];
            break;
        }
    if (!m)
        return;
    /* sing a chord tone (or a scale extension) of the dominant field */
    {
        const int8_t *v = sc->field[slot[slot_cur].voice];
        cand[0] = v[1] + 12;
        cand[1] = v[2] + 12;
        cand[2] = v[3] + 12;
        cand[3] = v[2] + 24;
        cand[4] = sc->scale[sc->nsc - 1] + 24;
    }
    m->freq = sc->root * fm_exp2f((float)cand[rng_next() % 5u] / 12.0f);
    if (m->freq > 900.0f)
        m->freq *= 0.5f;
    m->ratio = sc->ratio;
    m->ph = 0.0f;
    m->mph = 0.0f;
    m->gph = rng_f();
    m->peak = 0.16f + rng_f() * 0.07f;
    m->age = 0;
    m->atk = (uint32_t)((1.5f + rng_f() * 2.0f) * SRATE);
    m->dur = (uint32_t)((8.0f + rng_f() * 8.0f) * SRATE);
    m->ans_t = -1.0f;
    m->on = 1;
    gen_notes++;
}

static void drop_start(float freq, float level)
{
    int i;
    for (i = 0; i < NDROP; i++)
        if (!drop[i].on) {
            drop[i].on = 1;
            drop[i].ph = 0.0f;
            drop[i].freq = freq;
            drop[i].f = freq * 1.7f;
            drop[i].level = level;
            drop[i].age = 0;
            gen_notes++;
            return;
        }
}

static void sig_fire(float freq, float peak, float tau)
{
    int i, best = -1;
    uint32_t oldest = 0;
    for (i = 0; i < NSIG; i++) {
        if (!sigv[i].on) {
            best = i;
            break;
        }
        if (best < 0 || sigv[i].age > oldest) {
            oldest = sigv[i].age;
            best = i;
        }
    }
    sigv[best].on = 1;
    sigv[best].ph = 0.0f;
    sigv[best].freq = freq;
    sigv[best].peak = peak;
    sigv[best].tau = tau;
    sigv[best].wob_ph = rng_f() * TWO_PI;
    sigv[best].age = 0;
    sigv[best].atk = (uint32_t)(0.045f * SRATE);   /* a soft 45 ms rise: no click, no beep edge */
    sig_pings++;
    sig_last_at = (uint32_t)sample_now;
    sig_last_freq = freq;
}

/* a chord tone of the dominant field, x2, folded into the scene's beacon band */
static float sig_pick_freq(void)
{
    const scene_t *sc = &SCENE[set.scene];
    const int8_t *v = sc->field[slot[slot_cur].voice];
    int pick = 1 + (int)(rng_next() % 3u);
    float f = sc->root * fm_exp2f((float)v[pick] / 12.0f) * 2.0f;
    while (f > sc->sig_top)
        f *= 0.5f;
    while (f < 240.0f)
        f *= 2.0f;
    return f;
}

static void sig_one(void)
{
    float f = sig_pat_freq;
    if (rng_f() < 0.4f)
        f = sig_pick_freq();                 /* the pattern steps to another chord tone */
    f *= fm_exp2f((rng_f() * 2.0f - 1.0f) * 0.006f);   /* a few cents of human detune */
    sig_fire(f, (0.050f + 0.035f * TGT.sig_amt) * (0.85f + 0.3f * rng_f()),
             rng_f() < 0.5f ? 0.45f : 1.0f);           /* short and long pings, morse-like */
}

static uint32_t sig_gap_in_pattern(void)
{
    float g = 0.32f + rng_f() * 0.75f;
    if (rng_f() < 0.2f)
        g += 1.1f;                           /* the occasional longer pause inside a pattern */
    return (uint32_t)(g * SRATE);
}

static void sig_pattern(uint32_t now)
{
    float r = rng_f();
    sig_pats++;
    sig_pat_freq = sig_pick_freq();
    sig_one();                               /* the first ping fires at once */
    sig_left = (r < 0.30f ? 1 : r < 0.60f ? 2 : r < 0.85f ? 3 : 4) - 1;
    if (sig_left > 0)
        next_ping_at = now + sig_gap_in_pattern();
}

static void schedule(void)                   /* in the render, once per block */
{
    const scene_t *sc = &SCENE[set.scene];
    uint32_t now = (uint32_t)sample_now;
    if (!eng_playing)
        return;
    if ((int32_t)(now - next_mih_at) >= 0) {
        next_mih_at = now + (uint32_t)((4.0f + rng_f() * 5.0f) * SRATE);
        mih_start();
    }
    if ((int32_t)(now - next_drop_at) >= 0) {
        float gap = 9.0f + rng_f() * 16.0f;
        next_drop_at = now + (uint32_t)(gap * SRATE);
        if (rng_f() < 0.62f * sc->drop)
            drop_start(560.0f + rng_f() * 620.0f, 0.13f);
        else if (TGT.shimmer > 0.25f && rng_f() < 0.5f)
            drop_start(1900.0f + rng_f() * 1300.0f, 0.05f);   /* a shimmer ping */
    }
    if ((int32_t)(now - next_drift_at) >= 0) {   /* new drift targets every ~2 s */
        next_drift_at = now + (uint32_t)(2.0f * SRATE);
        SM.drift_cut = fm_exp2f((rng_f() * 2.0f - 1.0f) * 0.30f * TGT.drift);
        SM.drift_det = (rng_f() * 2.0f - 1.0f) * 0.004f * TGT.drift;
        SM.drift_gld = 0.7f + rng_f() * 0.6f * (0.4f + TGT.drift);
    }
    /* SIGNAL: patterns of 1-4 pings, then 8-25 s of silence (scaled by the
     * scene's density and KNOB4's amount). Below the amount threshold the
     * layer is simply off, and its clock stays deferred so turning the knob
     * up never fires a pattern instantly. */
    if (TGT.sig_amt > 0.02f && sc->sig > 0.01f) {
        if (sig_left > 0) {
            if ((int32_t)(now - next_ping_at) >= 0) {
                sig_one();
                if (--sig_left > 0)
                    next_ping_at = now + sig_gap_in_pattern();
            }
        } else if ((int32_t)(now - next_sig_at) >= 0) {
            float gap = (8.0f + rng_f() * 17.0f) / sc->sig * (1.15f - 0.5f * TGT.sig_amt);
            if (gap < 4.0f)
                gap = 4.0f;
            next_sig_at = now + (uint32_t)(gap * SRATE);
            if (rng_f() < 0.35f + 0.65f * TGT.sig_amt)
                sig_pattern(now);
        }
    } else {
        sig_left = 0;
        next_sig_at = now + (uint32_t)(2.0f * SRATE);
    }
    if (!eng_hold) {                         /* the field clock freezes while held */
        if (field_left <= APP_BLOCK)
            field_advance();
        else
            field_left -= APP_BLOCK;
    }
}

/* ----------------------------------------------------------------- render --- */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain_t = (float)master_q12 / 4096.0f * 8300000.0f * 1.6f;
    static float gain_now, eng_gain;
    static float blk_pk[4];              /* v0.3: per-block layer peaks for the mixer meters */
    static uint8_t frz_prev;             /* v0.3: the freeze engage edge */
    static float flfo_ph;                /* v0.4: the filter LFO's phase */
    const scene_t *sc = &SCENE[set.scene];
    schedule();
    for (i = 0; i < n; i++) {
        float s = 0.0f, ks = 0.0f, out, wet, wetR, echo_out, rev = 0.0f;
        float s_mih = 0.0f, s_guw = 0.0f, s_air = 0.0f, s_sig = 0.0f;   /* v0.3: per-layer sums for the mixer */
        int v, c, sl, j;
        /* glide the SELECT character: no zipper noise */
        SM.sel += (TGT.sel - SM.sel) * 0.0012f;
        SM.body += (TGT.body - SM.body) * 0.0006f;
        SM.detune += (TGT.detune - SM.detune) * 0.0008f;
        for (j = 0; j < 4; j++)
            SM.mix[j] += (TGT.mix[j] - SM.mix[j]) * 0.0015f;   /* layer gains glide: mutes fade, never click */
        SM.shim += (TGT.shim_fx - SM.shim) * 0.001f;
        SM.crush += (TGT.crush - SM.crush) * 0.0015f;
        SM.revecho += (TGT.revecho - SM.revecho) * 0.001f;    /* v0.4 */
        SM.wide += (TGT.wide - SM.wide) * 0.0015f;
        SM.frz += ((eng_freeze ? 1.0f : 0.0f) - SM.frz) * 0.00025f;   /* ~0.5 s freeze crossfades */
        gain_now += (gain_t - gain_now) * 0.004f;

        /* GUWAAN: the sub, breathing on the swell LFO */
        swell_ph += 1.0f / (TGT.swell_s * SRATE);
        if (swell_ph >= 1.0f)
            swell_ph -= 1.0f;
        {
            float sw = 0.5f + 0.5f * fm_sinf(TWO_PI * swell_ph);
            float sub_g = TGT.sub_w * (0.22f + 0.78f * sw) * 0.30f;
            sub_freq += (sc->root - sub_freq) * 0.00003f;
            slot[0].ph[0][0] += sub_freq / SRATE;
            if (slot[0].ph[0][0] >= 1.0f)
                slot[0].ph[0][0] -= 1.0f;
            s_guw += fm_sinf(TWO_PI * slot[0].ph[0][0]) * sub_g * (slot[0].g + slot[1].g > 0.01f ? 1.0f : 0.0f);
            /* a fifth above, much quieter, for body */
            slot[0].ph[0][1] += sub_freq * 1.5f / SRATE;
            if (slot[0].ph[0][1] >= 1.0f)
                slot[0].ph[0][1] -= 1.0f;
            s_guw += fm_sinf(TWO_PI * slot[0].ph[0][1]) * sub_g * 0.35f;
            /* the sub's 2nd harmonic (v0.2): weight a headphone can hear */
            slot[0].ph[0][2] += sub_freq * 2.0f / SRATE;
            if (slot[0].ph[0][2] >= 1.0f)
                slot[0].ph[0][2] -= 1.0f;
            s_guw += fm_sinf(TWO_PI * slot[0].ph[0][2]) * sub_g * 0.30f;
            /* the body oscillator (v0.2): root x4 - 294..523 Hz across the
             * scenes, squarely in the built-in speaker's range, so KNOB2's
             * fatness is audible even where the 73 Hz sub is not. A touch
             * of 2nd harmonic keeps it warm, not pure. */
            body_ph += sub_freq * 4.0f / SRATE;
            if (body_ph >= 1.0f)
                body_ph -= 1.0f;
            body3_ph += sub_freq * 3.0f / SRATE;
            if (body3_ph >= 1.0f)
                body3_ph -= 1.0f;
            body2_ph += sub_freq * 2.0f / SRATE;
            if (body2_ph >= 1.0f)
                body2_ph -= 1.0f;
            {
                float pres = slot[0].g + slot[1].g;
                float bg = SM.body * 0.19f * (0.55f + 0.45f * sw) * (pres > 1.0f ? 1.0f : pres);
                float bp = TWO_PI * body_ph;
                s_guw += (fm_sinf(bp) + 0.25f * fm_sinf(2.0f * bp)) * bg;
                s_guw += fm_sinf(TWO_PI * body3_ph) * bg * 0.55f;   /* root x3: the fifth's body */
                /* v0.2.1: root x2 (147-262 Hz across the scenes) - the
                 * low-mid warmth that makes KNOB2's instant component
                 * obvious on the built-in speaker within a second */
                s_guw += fm_sinf(TWO_PI * body2_ph) * bg * 0.85f;
            }
        }
        /* GUWAAN: the two field slots' pads (v0.2: detuned triples whose
         * spread and swell depth grow with KNOB2 - a chorus that fattens) */
        for (sl = 0; sl < 2; sl++) {
            slot[sl].g += (slot[sl].tgt - slot[sl].g) * 0.0000069f;   /* tau ~3.3 s */
            if (slot[sl].g < 0.001f && slot[sl].tgt == 0.0f)
                continue;
            for (j = 1; j < 4; j++) {
                float f = slot[sl].freq[j] * (1.0f + SM.drift_det);
                float pg = slot[sl].g * (0.038f + 0.012f * TGT.body) *
                           ((1.0f - TGT.swell_dep) + TGT.swell_dep * (0.5f + 0.5f * fm_sinf(TWO_PI * swell_ph)));
                slot[sl].ph[j][0] += f * (1.0f - SM.detune) / SRATE;
                if (slot[sl].ph[j][0] >= 1.0f)
                    slot[sl].ph[j][0] -= 1.0f;
                slot[sl].ph[j][1] += f / SRATE;
                if (slot[sl].ph[j][1] >= 1.0f)
                    slot[sl].ph[j][1] -= 1.0f;
                slot[sl].ph[j][2] += f * (1.0f + SM.detune) / SRATE;
                if (slot[sl].ph[j][2] >= 1.0f)
                    slot[sl].ph[j][2] -= 1.0f;
                s_guw += (fm_sinf(TWO_PI * slot[sl].ph[j][0]) + fm_sinf(TWO_PI * slot[sl].ph[j][1]) +
                      fm_sinf(TWO_PI * slot[sl].ph[j][2])) * pg;
            }
        }
        /* MIHYON: the gliding FM voices */
        for (v = 0; v < NMIH; v++) {
            mih_t *m = &mih[v];
            float env, ratio, ans_amp = 1.0f;
            if (!m->on)
                continue;
            m->age++;
            m->gph += 1.0f / (TGT.glide_per * SM.drift_gld * SRATE);
            if (m->gph >= 1.0f)
                m->gph -= 1.0f;
            {
                float expo = TGT.glide_dep * fm_sinf(TWO_PI * m->gph);
                if (m->ans_t >= 0.0f) {  /* v0.2.1: the KNOB1 answer sweep -
                                          * a ~1.2 s glide excursion in the
                                          * direction the knob was turned */
                    float bump;
                    m->ans_t += 1.0f / (1.2f * SRATE);
                    if (m->ans_t >= 1.0f)
                        m->ans_t = -1.0f;
                    bump = m->ans_t >= 0.0f ? fm_sinf(3.14159265f * m->ans_t) : 0.0f;
                    expo += (float)m->ans_dir * (0.45f + 0.55f * TGT.glide_dep) * bump;
                    ans_amp = 1.0f + 0.35f * bump;   /* the answer also speaks up */
                }
                if (expo > 2.4f)
                    expo = 2.4f;
                else if (expo < -2.4f)
                    expo = -2.4f;
                ratio = m->ratio * fm_exp2f(expo);
            }
            m->mph += m->freq * ratio / SRATE;
            if (m->mph >= 1.0f)
                m->mph -= 1.0f;
            m->ph += m->freq / SRATE;
            if (m->ph >= 1.0f)
                m->ph -= 1.0f;
            env = m->age < m->atk ? (float)m->age / (float)m->atk
                                  : fm_expf(-(float)(m->age - m->atk) / (4.5f * SRATE));
            if (m->age > m->atk && env < 0.0012f) {
                m->on = 0;
                continue;
            }
            s_mih += fm_sinf(TWO_PI * m->ph + 2.6f * env * fm_sinf(TWO_PI * m->mph)) * env * m->peak * ans_amp;
        }
        /* the keyboard: held notes sustain, released notes melt (tau ~6 s) */
        for (v = 0; v < NKEYV; v++) {
            keyv_t *kv = &keyv[v];
            float tgt;
            if (!kv->st)
                continue;
            kv->mph += kv->freq * 2.0f / SRATE;
            if (kv->mph >= 1.0f)
                kv->mph -= 1.0f;
            kv->ph += kv->freq / SRATE;
            if (kv->ph >= 1.0f)
                kv->ph -= 1.0f;
            tgt = kv->st == 1 ? 0.17f * kv->vel : 0.0f;
            kv->level += (tgt - kv->level) * (kv->st == 1 ? 0.0012f : 0.000038f);
            if (kv->st == 2 && kv->level < 0.0012f) {
                kv->st = 0;
                continue;
            }
            ks += fm_sinf(TWO_PI * kv->ph + 1.8f * fm_sinf(TWO_PI * kv->mph)) * kv->level;
        }
        /* AIR: band-passed noise, slowly wandering; K4 adds a high sparkle */
        {
            float nz = noise_f();
            float bp;
            air_lp += 0.045f * (nz - air_lp);
            air_hp += 0.0018f * (air_lp - air_hp);
            bp = air_lp - air_hp;
            air_am_ph += 0.23f / SRATE;
            if (air_am_ph >= 1.0f)
                air_am_ph -= 1.0f;
            s_air += bp * sc->air * 0.16f * (0.7f + 0.3f * fm_sinf(TWO_PI * air_am_ph));
            s_air += (nz - air_lp) * TGT.shimmer * 0.028f;
        }
        /* water drops */
        for (v = 0; v < NDROP; v++) {
            drop_t *d = &drop[v];
            float env;
            if (!d->on)
                continue;
            d->age++;
            d->f += (d->freq * 0.55f - d->f) * 0.004f;
            d->ph += d->f / SRATE;
            if (d->ph >= 1.0f)
                d->ph -= 1.0f;
            env = fm_expf(-(float)d->age / (0.9f * SRATE));
            if (env < 0.0012f) {
                d->on = 0;
                continue;
            }
            s_air += fm_sinf(TWO_PI * d->ph) * env * d->level *
                 (d->age < 200u ? (float)d->age / 200.0f : 1.0f);
        }
        /* SIGNAL: the beacon pings. A soft sine, a very slow pitch wobble
         * (a few cents, like a real transmitter drifting), and a long decay
         * into the shared echo/reverb - that wash is what makes them far. */
        for (v = 0; v < NSIG; v++) {
            sig_t *p = &sigv[v];
            float env;
            if (!p->on)
                continue;
            p->age++;
            p->ph += p->freq * (1.0f + 0.0035f * fm_sinf(p->wob_ph +
                             TWO_PI * (float)p->age / (6.0f * p->tau * SRATE))) / SRATE;
            if (p->ph >= 1.0f)
                p->ph -= 1.0f;
            if (p->age < p->atk) {
                float x = (float)p->age / (float)p->atk;
                env = x * x * (3.0f - 2.0f * x);
            } else {
                env = fm_expf(-(float)(p->age - p->atk) / (p->tau * SRATE));
            }
            if (p->age > p->atk && env < 0.0012f) {
                p->on = 0;
                continue;
            }
            s_sig += fm_sinf(TWO_PI * p->ph) * env * p->peak;
        }

        /* v0.3: the mixer - the four layers through their glided gains */
        {
            float c0 = s_mih * SM.mix[0], c1 = s_guw * SM.mix[1];
            float c2 = s_air * SM.mix[2], c3 = s_sig * SM.mix[3];
            if (fm_fabsf(c0) > blk_pk[0]) blk_pk[0] = fm_fabsf(c0);
            if (fm_fabsf(c1) > blk_pk[1]) blk_pk[1] = fm_fabsf(c1);
            if (fm_fabsf(c2) > blk_pk[2]) blk_pk[2] = fm_fabsf(c2);
            if (fm_fabsf(c3) > blk_pk[3]) blk_pk[3] = fm_fabsf(c3);
            s = c0 + c1 + c2 + c3;
        }
        /* FREEZE (v0.3): while live, the ring records the layer mix; while
         * frozen, the two Hann heads loop it and the live layers bleed
         * through at 15%, so the performance continues over the held wash.
         * The blend is SM.frz (glided above): no capture or release click. */
        if (!eng_freeze) {
            float q = s > 1.3f ? 1.3f : s < -1.3f ? -1.3f : s;
            frz_buf[frz_pos] = (int16_t)(q * 24000.0f);
            frz_pos = (frz_pos + 1u) % FRZ_LEN;
        } else if (!frz_prev)
            frz_p = (float)frz_pos;     /* playback starts where the recording stopped */
        frz_prev = eng_freeze;
        if (SM.frz > 0.003f) {
            float p2, g1, g2, loop;
            frz_p += 1.0f;
            if (frz_p >= (float)FRZ_LEN)
                frz_p -= (float)FRZ_LEN;
            p2 = frz_p + (float)(FRZ_LEN / 2);
            if (p2 >= (float)FRZ_LEN)
                p2 -= (float)FRZ_LEN;
            g1 = 0.5f - 0.5f * fm_cosf(TWO_PI * frz_p / (float)FRZ_LEN);
            g2 = 0.5f - 0.5f * fm_cosf(TWO_PI * p2 / (float)FRZ_LEN);
            loop = ((float)frz_buf[(uint32_t)frz_p] * g1 +
                    (float)frz_buf[(uint32_t)p2] * g2) * (1.0f / 24000.0f);
            s = s * (1.0f - SM.frz * 0.85f) + loop * SM.frz;
        }

        /* PLAY stops the generation (a ~0.3 s fade); the keys still play */
        eng_gain += ((eng_playing ? 1.0f : 0.0f) - eng_gain) * 0.001f;
        s = (s * eng_gain + ks) * 0.5f;   /* headroom: the wash stays out of saturation */

        /* CRUSH (v0.3): sample-hold + bit reduction on the dry mix. The wet
         * mix is SM.crush itself (glided), so engaging ramps in. */
        if (SM.crush > 0.003f) {
            float a = SM.crush;
            uint32_t div = 1u + (uint32_t)(a * a * 24.0f);
            float step = fm_exp2f(-(16.0f - 10.0f * a));   /* 16 bits .. 6 bits */
            float q;
            if (++crush_cnt >= div) {
                crush_cnt = 0;
                crush_val = s;
            }
            q = (float)(int32_t)(crush_val / step + (crush_val >= 0.0f ? 0.5f : -0.5f)) * step;
            s = s * (1.0f - a) + q * a;
        }

        /* the SELECT main filter over the whole scene. v0.4: the filter
         * LFO slowly works the cutoff while it is on - the sound keeps
         * moving with nobody's hands on the panel. It modulates the
         * SELECT character itself, so it sweeps LP -> open -> HP and
         * works even from the centre. */
        {
            float sel_eff = SM.sel;
            if (set.flfo & 1u) {
                flfo_ph += 1.0f / (TGT.flfo_per * SRATE);
                if (flfo_ph >= 1.0f)
                    flfo_ph -= 1.0f;
                sel_eff += TGT.flfo_amp * fm_sinf(TWO_PI * flfo_ph);
                if (sel_eff > 1.0f)
                    sel_eff = 1.0f;
                else if (sel_eff < -1.0f)
                    sel_eff = -1.0f;
            }
            out = main_filter(s, sel_eff, SM.drift_cut);
        }

        /* the space wash (K3): tape echo, capped feedback, degrading repeats */
        echo_out = echo_buf[echo_pos];
        echo_lp += 0.38f * (echo_out - echo_lp);
        /* v0.4: a second tap, half the delay away - the width's far side */
        echo_lp2 += 0.38f * (echo_buf[(echo_pos + ECHO_LEN / 2) % ECHO_LEN] - echo_lp2);
        echo_buf[echo_pos] = out * TGT.echo_send + echo_lp * (TGT.echo_fb > 0.82f ? 0.82f : TGT.echo_fb);
        echo_pos = (echo_pos + 1u) % ECHO_LEN;
        /* SHIMMER (v0.3): the dry signal fills the granular line every
         * sample, so the octave-up send is always primed */
        shim_buf[shim_pos] = out;
        shim_pos = (shim_pos + 1u) & (SHIM_LEN - 1u);
        wet = out + echo_lp * 0.5f;
        wetR = out + (echo_lp * (1.0f - SM.wide) + echo_lp2 * SM.wide) * 0.5f;
        /* REVERSE ECHO (v0.4): the post-filter signal is recorded into a
         * ring; two Hann grains play the last ~0.5 s backwards, blooming
         * into the wet path ahead of the reverb. */
        {
            float q = out > 1.3f ? 1.3f : out < -1.3f ? -1.3f : out;
            rev_buf[rev_pos] = (int16_t)(q * 24000.0f);
            rev_pos = (rev_pos + 1u) % REV_LEN;
        }
        if (SM.revecho > 0.003f) {
            float a2, g1, g2, ro;
            int32_t ix1, ix2;
            rev_age += 1.0f;
            if (rev_age >= REV_GRAIN)
                rev_age -= REV_GRAIN;
            a2 = rev_age + REV_GRAIN * 0.5f;
            if (a2 >= REV_GRAIN)
                a2 -= REV_GRAIN;
            g1 = 0.5f - 0.5f * fm_cosf(TWO_PI * rev_age / REV_GRAIN);
            g2 = 0.5f - 0.5f * fm_cosf(TWO_PI * a2 / REV_GRAIN);
            ix1 = (int32_t)rev_pos - (int32_t)rev_age;
            if (ix1 < 0)
                ix1 += REV_LEN;
            ix2 = (int32_t)rev_pos - (int32_t)a2;
            if (ix2 < 0)
                ix2 += REV_LEN;
            ro = ((float)rev_buf[ix1] * g1 + (float)rev_buf[ix2] * g2) * (1.0f / 24000.0f);
            wet += ro * SM.revecho * 0.8f;
            wetR += ro * SM.revecho * 0.8f;
        }
        /* reverb: 4 combs + 2 allpasses (Schroeder). It also runs for the
         * shimmer send alone, so SHIMMER still speaks with SPACE at zero. */
        if (TGT.rev_send > 0.01f || SM.shim > 0.003f) {
            float rin = wet * TGT.rev_send * 0.42f;
            if (SM.shim > 0.003f) {
                /* two double-speed heads, Hann-windowed half a window
                 * apart: a continuous octave-up copy of the dry signal */
                float ph2, g1, g2, d1, d2, o1, o2;
                uint32_t i1, i2;
                shim_ph += 1.0f / (float)SHIM_WIN;
                if (shim_ph >= 1.0f)
                    shim_ph -= 1.0f;
                ph2 = shim_ph + 0.5f;
                if (ph2 >= 1.0f)
                    ph2 -= 1.0f;
                g1 = 0.5f - 0.5f * fm_cosf(TWO_PI * shim_ph);
                g2 = 0.5f - 0.5f * fm_cosf(TWO_PI * ph2);
                d1 = (1.0f - shim_ph) * (float)SHIM_WIN;
                d2 = (1.0f - ph2) * (float)SHIM_WIN;
                i1 = (shim_pos + SHIM_LEN - (uint32_t)d1) & (SHIM_LEN - 1u);
                i2 = (shim_pos + SHIM_LEN - (uint32_t)d2) & (SHIM_LEN - 1u);
                o1 = shim_buf[i1] * (1.0f - (d1 - (float)(uint32_t)d1)) +
                     shim_buf[(i1 + SHIM_LEN - 1u) & (SHIM_LEN - 1u)] * (d1 - (float)(uint32_t)d1);
                o2 = shim_buf[i2] * (1.0f - (d2 - (float)(uint32_t)d2)) +
                     shim_buf[(i2 + SHIM_LEN - 1u) & (SHIM_LEN - 1u)] * (d2 - (float)(uint32_t)d2);
                rin += (o1 * g1 + o2 * g2) * SM.shim * (0.25f + 0.75f * TGT.rev_send) * 0.9f;
            }
            for (c = 0; c < 4; c++) {
                uint32_t p = comb_pos[c];
                float y = comb_buf[c][p];
                comb_buf[c][p] = rin + y * 0.62f;
                comb_pos[c] = (p + 1u) % COMB_LEN[c];
                rev += y;
            }
            rev *= 0.25f;
            {
                /* v0.4: the two allpass taps are kept separately - the
                 * right channel leans on the earlier tap as width grows */
                float ra, rb;
                uint32_t p = ap_pos[0];
                float y = ap_buf[0][p];
                float x = rev + y * 0.5f;
                ap_buf[0][p] = x;
                ap_pos[0] = (p + 1u) % AP_LEN[0];
                ra = y - x * 0.5f;
                p = ap_pos[1];
                y = ap_buf[1][p];
                x = ra + y * 0.5f;
                ap_buf[1][p] = x;
                ap_pos[1] = (p + 1u) % AP_LEN[1];
                rb = y - x * 0.5f;
                wet += rb * 0.5f;
                wetR += (rb * (1.0f - SM.wide) + ra * SM.wide) * 0.5f;
            }
        }

        /* ceiling: gentle saturation, then a limiter that only ever turns
         * down. The ceiling is 0.55, not 0.92: the gain staging below
         * multiplies by 1.6, so 0.55 lands at ~-1.7 dBFS at the output.
         * v0.4: the right channel first passes the decorrelator (its read
         * delay grows with WIDE; at width 0 it reads the sample just
         * written, so the mono render is bit-identical to v0.3), and the
         * limiter judges the louder of the two channels. */
        {
            float oL, oR, m, gL, gR;
            dec_buf[dec_pos] = wetR;
            {
                float rp = (float)dec_pos - SM.wide * 11.0f;
                uint32_t i0;
                float fr;
                while (rp < 0.0f)
                    rp += 32.0f;
                i0 = (uint32_t)rp;
                fr = rp - (float)i0;
                wetR = dec_buf[i0] * (1.0f - fr) + dec_buf[(i0 + 1u) & 31u] * fr;
            }
            dec_pos = (dec_pos + 1u) & 31u;
            oL = fm_tanhf(wet * 0.9f);
            oR = fm_tanhf(wetR * 0.9f);
            m = fm_fabsf(oL) > fm_fabsf(oR) ? fm_fabsf(oL) : fm_fabsf(oR);
            if (m * SM.lim > 0.55f)
                SM.lim = 0.55f / m;
            else if (SM.lim < 1.0f)
                SM.lim += (1.0f - SM.lim) * 0.0004f;
            gL = oL * SM.lim * gain_now;
            gR = oR * SM.lim * gain_now;
            lr[2 * i] = (int32_t)gL;
            lr[2 * i + 1] = (int32_t)gR;
        }
        sample_now++;
    }
    {                                    /* v0.3: publish the layer meters */
        int j;
        for (j = 0; j < 4; j++) {
            eng_lvl[j] = blk_pk[j] > eng_lvl[j] ? blk_pk[j] : eng_lvl[j] * 0.93f;
            blk_pk[j] = 0.0f;
        }
    }
}

void app_panic(void)
{
    int i;
    for (i = 0; i < NMIH; i++)
        mih[i].on = 0;
    for (i = 0; i < NKEYV; i++)
        keyv[i].st = 0;
    for (i = 0; i < NDROP; i++)
        drop[i].on = 0;
    for (i = 0; i < NSIG; i++)
        sigv[i].on = 0;
    sig_left = 0;
    eng_freeze = 0;                      /* v0.3: a frozen loop must not survive a panic */
    SM.frz = 0.0f;
}

/* ------------------------------------------------------- knob macro targets - */
/* v0.2.1: a knob turn must answer NOW, not one slow cycle later (the
 * user's device verdict on v0.2: K1/K2 "do almost nothing" - their DSP
 * was real but its time constants were tens of seconds). KNOB1 restarts
 * a ~1.2 s glide sweep on every sounding mihyon voice, in the direction
 * turned - or starts one voice fast if the layer is momentarily silent.
 * KNOB2 restarts the swell LFO on the steep part of its cycle, rising
 * when the knob went up, falling when it went down, so the new period
 * and depth are heard from this second. Called from params_from_panel()
 * whenever the panel values actually changed. */
static void knob_answers(void)
{
    static int pk1 = -1, pk2 = -1;
    if (rc.on) {                             /* v0.4: a recall glide is not a knob turn */
        pk1 = set.k1;
        pk2 = set.k2;
        return;
    }
    if (pk1 >= 0 && set.k1 != pk1) {
        int dir = set.k1 > pk1 ? 1 : -1, i, any = 0;
        for (i = 0; i < NMIH; i++)
            if (mih[i].on) {
                mih[i].ans_t = 0.0f;
                mih[i].ans_dir = (int8_t)dir;
                any = 1;
            }
        if (!any) {                      /* nothing sounding: answer with a fresh voice */
            mih_start();
            for (i = 0; i < NMIH; i++)
                if (mih[i].on) {
                    mih[i].atk = (uint32_t)(0.25f * SRATE);
                    mih[i].ans_t = 0.0f;
                    mih[i].ans_dir = (int8_t)dir;
                }
        }
    }
    if (pk2 >= 0 && set.k2 != pk2)
        swell_ph = set.k2 > pk2 ? 0.95f : 0.30f;
    pk1 = set.k1;
    pk2 = set.k2;
}

/* Called from the UI at frame rate: the panel's numbers become targets, and
 * the wide middle of every knob is the sweet spot. */
static void params_from_panel(void)
{
    float k1 = (float)set.k1 / 127.0f, k2 = (float)set.k2 / 127.0f;
    float k3 = (float)set.k3 / 127.0f, k4 = (float)set.k4 / 127.0f;
    TGT.sel = (float)set.sel / 48.0f;
    TGT.glide_per = 18.0f - k1 * 16.0f;              /* 18 s .. 2 s cycles (v0.2.1: the top end runs far faster) */
    TGT.glide_dep = 0.15f + k1 * 1.50f;              /* the ratio sweeps up to ~1.65 octaves (v0.2.1) */
    TGT.sub_w = 0.25f + k2 * 0.75f;
    TGT.swell_s = 36.0f - k2 * 16.0f;                /* 36 s .. 20 s swells */
    TGT.body = k2;                                   /* v0.2: the root-x4 body's weight */
    TGT.detune = 0.0012f + k2 * 0.0028f;             /* pad spread +-0.12% .. +-0.40% (v0.2.1) */
    TGT.swell_dep = 0.35f + k2 * 0.45f;              /* v0.2: swell depth 0.35 .. 0.80 (wide, but the trough keeps a floor) */
    TGT.echo_send = k3 * 0.8f;
    TGT.echo_fb = 0.30f + k3 * 0.48f;                /* capped again in the render */
    TGT.rev_send = k3 * SCENE[set.scene].rev;
    TGT.drift = k4 * k4;                             /* gentle until the top of the travel */
    TGT.shimmer = k4;
    /* v0.2: KNOB4's travel above ~15% is the SIGNAL layer's amount - the
     * beacons fade in with the drift, they are never a separate switch. */
    TGT.sig_amt = k4 < 0.15f ? 0.0f : (k4 - 0.15f) / 0.85f;
    /* v0.3: the mixer and FX targets (glided in the render) */
    {
        int i;
        for (i = 0; i < 4; i++)
            TGT.mix[i] = (set.mute >> i & 1u) ? 0.0f : (float)set.mix[i] / 127.0f;
    }
    TGT.shim_fx = (float)set.shimmer / 127.0f;
    TGT.crush = (float)set.crush / 127.0f;
    /* v0.4 targets */
    TGT.revecho = (float)set.revecho / 127.0f;
    TGT.wide = (float)set.wide / 127.0f;
    TGT.flfo_per = 2.0f * fm_exp2f(4.322f * (float)set.flfo_rate / 127.0f);   /* 2 s .. 40 s */
    TGT.flfo_amp = (float)set.flfo_dep / 127.0f * 0.5f;                       /* up to half the SELECT travel */
    knob_answers();
}

void app_init(void)
{
    settings_load();
    rng_s ^= plat_ms() * 2654435761u ^ 0x9E3779B9u;
    if (!rng_s)
        rng_s = 0x1F123BB5u;
    eng_playing = set.playing;
    SM.lim = 1.0f;
    SM.drift_cut = 1.0f;
    SM.drift_gld = 1.0f;
    params_from_panel();
    SM.sel = TGT.sel;
    slot_assign(0, 0);
    slot_assign(1, 1);
    slot[0].g = 0.0f;
    slot[0].tgt = 1.0f;                  /* the first field fades in over ~10 s */
    slot[1].g = 0.0f;
    slot[1].tgt = 0.0f;
    slot_cur = 0;
    sub_freq = SCENE[set.scene].root;
    field_left = (uint32_t)(SCENE[set.scene].field_s * SRATE);
    next_mih_at = (uint32_t)(1.0f * SRATE);
    next_drop_at = (uint32_t)(7.0f * SRATE);
    next_drift_at = (uint32_t)(2.0f * SRATE);
    next_sig_at = (uint32_t)(3.0f * SRATE);
    SM.body = TGT.body;
    SM.detune = TGT.detune;
    {                                    /* v0.3: start on the saved mix, no boot sweep */
        int i;
        for (i = 0; i < 4; i++)
            SM.mix[i] = TGT.mix[i];
        SM.shim = TGT.shim_fx;
        SM.crush = TGT.crush;
        SM.revecho = TGT.revecho;        /* v0.4 */
        SM.wide = TGT.wide;
    }
}

/* --------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t rec_t;                          /* REC went down (0: not held) */
    uint8_t rec_long;                        /* the long-press already fired */
    int32_t turns[NE];
    int32_t key_oct;
    uint32_t last_draw;
    uint8_t redraw;
    /* v0.3: pages and the live-performance controls */
    uint8_t page;                            /* 0 normal, 1 mixer, 2 FX */
    uint8_t fx_bank;                         /* v0.4: the FX page's bank (0 A, 1 B) */
    uint32_t arp_t, home_t;                  /* ARP / HOME went down (0: not held) */
    uint8_t arp_long, home_long;
    uint32_t seq_t;                          /* v0.4: SEQ went down (0: not held) */
    uint8_t seq_long;
    uint32_t arp_next, arp_off_at;           /* the arp's step and gate clocks (ms) */
    int32_t arp_cur;                         /* the note the arp is gating (-1000: none; MIDI notes are negative) */
    int32_t arp_pos, arp_dir;                /* the UP-DOWN walk state */
    int8_t arp_led_key;                      /* the key the current step lights */
    uint32_t arp_led_until;
} ui;
static volatile uint32_t arp_steps;          /* arp steps since boot (the tests read it) */
static volatile int32_t arp_last_key = -1;   /* the last key the arp played */

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void save_settings(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
        set_dirty = 0;
        app_say("SAVED", "");
    } else {
        app_say("SAVE", "FAILED");
    }
}

/* the key LEDs: the whole keyboard breathes at dim level (a ~9.6 s cycle,
 * plat_dim_ns between 900 and 2600 ns), pressed keys glow. Brightness
 * only - the FM-1's key LEDs have no colour to give. */
static float key_freq(int k);                /* below: the ping's key light needs it */
static int nearest_key(float f)              /* the panel key closest in pitch to f */
{
    int bk = -1, k2;
    float bd = 1e9f;
    for (k2 = 0; k2 < NKEYS; k2++) {
        float r = f / key_freq(k2);
        float d = r > 1.0f ? r : 1.0f / r;
        if (d < bd) {
            bd = d;
            bk = k2;
        }
    }
    return bk;
}
static int32_t led_ns_cur = -1;
static uint32_t led_dim_m, led_lit_m;
static int popcount(uint32_t x)
{
    int n = 0;
    while (x) {
        n += (int)(x & 1u);
        x >>= 1;
    }
    return n;
}
static uint32_t led_mid_m, led_seen_m;       /* v0.3: the mid mask, and every key lit since boot */
static void lights(uint32_t btn, uint32_t keys)
{
    uint32_t lit = 0, dim = (1u << NKEYS) - 1u, b, db, mid = 0;
    uint32_t now = plat_ms();
    int stage = (set.feat >> 3) & 1u;
    int32_t ns;
    int k;
    for (k = 0; k < NKEYS; k++)
        if (keys >> k & 1u)
            lit |= 1u << k;
    /* v0.3: the arp's current step lights its key while the gate is open */
    if (ui.arp_led_key >= 0 && (int32_t)(ui.arp_led_until - now) > 0)
        lit |= 1u << ui.arp_led_key;
    b = btn | (set_dirty ? 1u << B_SAVE : 0u) | (eng_hold ? 1u << B_REC : 0u) |
        (eng_freeze ? 1u << B_GLO : 0u) | ((set.feat & 1u) ? 1u << B_ARP : 0u) |
        (ui.page == 1 ? 1u << B_EDIT : 0u) | (ui.page == 2 ? 1u << B_FX : 0u) |
        (stage ? 1u << B_HOME : 0u) | ((set.flfo & 1u) ? 1u << B_LFO : 0u);
    b &= (1u << NB) - 1u;
    {
        uint32_t play = 1u << B_PLAY;
        int play_lit = (b & play) != 0 || eng_playing;
        if (eng_playing)
            b |= play;
        b &= ~play;
        if (play_lit)
            b |= LED_PLAY_GREEN;
        db = ((1u << NB) - 1u) & ~b & ~play;
        if (!play_lit)
            db |= LED_PLAY_GREEN;
    }
    if (ui.page == 1) {
        /* v0.3, the mixer page: the keyboard is four layer meters -
         * keys 0-6 MIHYON, 7-13 GUWAAN, 14-20 AIR, 21-26 SIGNAL. A live
         * layer's zone breathes at dim; while it sounds, the zone rises
         * to mid; a muted layer's zone is dark. Readable across a stage. */
        static const uint32_t zone[4] = {0x7Fu, 0x7Fu << 7, 0x7Fu << 14, 0x3Fu << 21};
        int i;
        dim = 0;
        for (i = 0; i < 4; i++) {
            if (set.mute >> i & 1u)
                continue;
            dim |= zone[i];
            if (eng_lvl[i] > 0.05f)
                mid |= zone[i];
        }
    }
    plat_lights(b, lit, db, dim, 1);
    /* a sounding beacon lights its nearest key at mid level for ~1.2 s -
     * the transmitter blinking out there, answered on the keyboard */
    if (sig_last_freq > 0.0f &&
        (uint32_t)sample_now - sig_last_at < (uint32_t)(1.2f * SRATE)) {
        int bk = nearest_key(sig_last_freq);
        if (bk >= 0)
            mid |= 1u << bk;
    }
    /* FREEZE: the whole keyboard holds a steady soft glow (mid level) */
    if (eng_freeze)
        mid = (1u << NKEYS) - 1u;
    plat_lights_mid(0, mid);
    led_dim_m = dim;
    led_lit_m = lit;
    led_mid_m = mid;
    led_seen_m |= lit;
    /* the breathing: gentle in normal mode (900..2600 ns); STAGE mode
     * (HOME held) runs the dim pulse near the top of its range
     * (3000..3800 ns) for dark venues. Frozen: steady, no breathing. */
    if (eng_freeze)
        ns = stage ? 3600 : 1750;
    else if (stage)
        ns = 3400 + (int32_t)(400.0f * fm_sinf((float)(now % 9600u) * TWO_PI / 9600.0f));
    else
        ns = 1750 + (int32_t)(850.0f * fm_sinf((float)(now % 9600u) * TWO_PI / 9600.0f));
    if (led_ns_cur < 0 || ns - led_ns_cur > 40 || led_ns_cur - ns > 40) {
        plat_dim_ns((uint32_t)ns);
        led_ns_cur = ns;
    }
}

static float key_freq(int k)
{
    const scene_t *sc = &SCENE[set.scene];
    int deg = k % sc->nsc, oct = k / sc->nsc;
    float f = sc->root * 2.0f * fm_exp2f((float)(sc->scale[deg] + 12 * (oct + (int)ui.key_oct)) / 12.0f);
    while (f > 1250.0f)
        f *= 0.5f;
    return f;
}

/* A voice for a marker: a panel key (0..26) or a MIDI note (-1-note).
 * v0.4: MIDI notes start here too, with their velocity. A voice already
 * sounding for the marker retriggers (the arp's repeated steps). */
static void voice_start(int marker, float freq, float vel)
{
    int i, best = -1;
    for (i = 0; i < NKEYV; i++)
        if (keyv[i].st == 1 && keyv[i].key == marker) {
            keyv[i].ph = 0.0f;
            keyv[i].mph = 0.0f;
            return;
        }
    for (i = 0; i < NKEYV; i++)
        if (!keyv[i].st) {
            best = i;
            break;
        }
    if (best < 0) {                          /* steal the quietest voice */
        float bl = 1e9f;
        for (i = 0; i < NKEYV; i++)
            if (keyv[i].level < bl) {
                bl = keyv[i].level;
                best = i;
            }
    }
    keyv[best].key = (int8_t)marker;
    keyv[best].freq = freq;
    keyv[best].vel = vel;
    keyv[best].ph = 0.0f;
    keyv[best].mph = 0.0f;
    keyv[best].st = 1;
}

static void key_on(int k)
{
    voice_start(k, key_freq(k), 1.0f);
}

static void key_off(int k)
{
    int i;
    for (i = 0; i < NKEYV; i++)
        if (keyv[i].st == 1 && keyv[i].key == k)
            keyv[i].st = 2;                  /* melt: the render's slow release */
}

/* ------------------------------------------------- v0.3: mixer and the arp --- */
static const char *const LAYER_NAME[4] = {"MIHYON", "GUWAAN", "AIR", "SIGNAL"};
static const char *const ARP_PATN[3] = {"UP", "UP-DOWN", "RANDOM"};

/* v0.4: the MIDI notes held for the arp (scale-snapped at note-on) */
static int mn_note[16], mn_n;
static float mn_freq[16], mn_vel[16];

static void mute_toggle(int i)
{
    set.mute ^= (uint8_t)(1u << i);
    set_dirty = 1;
    app_say(LAYER_NAME[i], (set.mute >> i & 1u) ? "MUTED" : "ON");
    ui.redraw = 1;
}

/* The arpeggiator owns the keyboard voice while it is on: the held keys
 * cycle at ambient rates (0.25 s .. 6 s steps - the FX page's ARP RATE),
 * gated by ARP GATE. The held keys are already scale-locked, so every
 * step fits the drone by construction. */
static float arp_step_ms(void)
{
    return 250.0f * fm_exp2f(4.585f * (float)set.arp_rate / 127.0f);   /* a x24 range */
}

static void arp_step(uint32_t now)
{
    int mk[NKEYS + 16], n = 0, k, pick, i, j;
    float fq[NKEYS + 16], vl[NKEYS + 16];
    float step = arp_step_ms();
    for (k = 0; k < NKEYS; k++)
        if (keys_down >> k & 1u) {
            mk[n] = k;                       /* panel keys: ascending = ascending pitch */
            fq[n] = key_freq(k);
            vl[n] = 1.0f;
            n++;
        }
    for (i = 0; i < mn_n; i++) {             /* v0.4: the held MIDI notes join the cycle */
        mk[n] = -1 - mn_note[i];
        fq[n] = mn_freq[i];
        vl[n] = mn_vel[i];
        n++;
    }
    if (!n) {
        ui.arp_next = now + 100u;
        return;
    }
    for (i = 1; i < n; i++) {                /* insertion sort by pitch */
        int m2 = mk[i];
        float f2 = fq[i], v2 = vl[i];
        for (j = i - 1; j >= 0 && fq[j] > f2; j--) {
            mk[j + 1] = mk[j];
            fq[j + 1] = fq[j];
            vl[j + 1] = vl[j];
        }
        mk[j + 1] = m2;
        fq[j + 1] = f2;
        vl[j + 1] = v2;
    }
    switch ((set.feat >> 1) & 3u) {
    case 1:                                  /* UP-DOWN: ping-pong, the endpoints once */
        if (ui.arp_pos >= n)
            ui.arp_pos = n - 1;
        if (ui.arp_pos < 0)
            ui.arp_pos = 0;
        pick = ui.arp_pos;
        ui.arp_pos += ui.arp_dir;
        if (ui.arp_pos >= n - 1) {
            ui.arp_pos = n - 1;
            ui.arp_dir = -1;
        }
        if (ui.arp_pos <= 0) {
            ui.arp_pos = 0;
            ui.arp_dir = 1;
        }
        break;
    case 2:                                  /* RANDOM: never the same note twice in a row */
        pick = (int)(rng_next() % (uint32_t)n);
        if (n > 1 && mk[pick] == arp_last_key)
            pick = (pick + 1) % n;
        break;
    default:                                 /* UP */
        pick = (int)(arp_steps % (uint32_t)n);
        break;
    }
    k = mk[pick];
    voice_start(k, fq[pick], vl[pick]);
    ui.arp_cur = k;
    arp_last_key = k;
    arp_steps++;
    ui.arp_off_at = now + (uint32_t)(step * (0.15f + 0.85f * (float)set.arp_gate / 127.0f));
    ui.arp_next = now + (uint32_t)step;
    ui.arp_led_key = (int8_t)(k >= 0 ? k : nearest_key(fq[pick]));
    ui.arp_led_until = ui.arp_off_at;
}

static void arp_set(int on)
{
    int i, k;
    if (on) {
        set.feat |= 1u;
        for (i = 0; i < NKEYV; i++)          /* hand the notes over: they melt, the arp restarts them */
            if (keyv[i].st == 1)
                keyv[i].st = 2;
        ui.arp_next = plat_ms();             /* the first step lands at once */
        ui.arp_pos = 0;
        ui.arp_dir = 1;
        app_say("ARP", "ON");
    } else {
        set.feat &= (uint8_t)~1u;
        if (ui.arp_cur != -1000) {
            key_off(ui.arp_cur);
            ui.arp_cur = -1000;
        }
        ui.arp_led_key = -1;
        for (k = 0; k < NKEYS; k++) {        /* hand the keys back to the fingers */
            int has = 0;
            if (!(keys_down >> k & 1u))
                continue;
            for (i = 0; i < NKEYV; i++)
                if (keyv[i].st == 1 && keyv[i].key == k)
                    has = 1;
            if (!has)
                key_on(k);
        }
        app_say("ARP", "OFF");
    }
    set_dirty = 1;
    ui.redraw = 1;
}

static void fx_knob(int idx, int32_t e)      /* the FX page's four knobs, per bank */
{
    if (ui.fx_bank) {                         /* v0.4, bank B */
        switch (idx) {
        case 0: set.revecho = (uint8_t)clamp(set.revecho + e * 4, 0, 127); break;
        case 1: set.wide = (uint8_t)clamp(set.wide + e * 4, 0, 127); break;
        case 2: set.flfo_dep = (uint8_t)clamp(set.flfo_dep + e * 4, 0, 127); break;
        default: set.flfo_rate = (uint8_t)clamp(set.flfo_rate + e * 4, 0, 127); break;
        }
    } else {
        switch (idx) {
        case 0: set.shimmer = (uint8_t)clamp(set.shimmer + e * 4, 0, 127); break;
        case 1: set.crush = (uint8_t)clamp(set.crush + e * 4, 0, 127); break;
        case 2: set.arp_rate = (uint8_t)clamp(set.arp_rate + e * 4, 0, 127); break;
        default: set.arp_gate = (uint8_t)clamp(set.arp_gate + e * 4, 0, 127); break;
        }
    }
    set_dirty = 1;
}

static void macro_knob(int idx, int32_t e)   /* the normal page's four macros */
{
    switch (idx) {
    case 0: set.k1 = (uint8_t)clamp(set.k1 + e * 4, 0, 127); break;
    case 1: set.k2 = (uint8_t)clamp(set.k2 + e * 4, 0, 127); break;
    case 2: set.k3 = (uint8_t)clamp(set.k3 + e * 4, 0, 127); break;
    default: set.k4 = (uint8_t)clamp(set.k4 + e * 4, 0, 127); break;
    }
    set_dirty = 1;
}

/* ------------------------------------------- v0.4: snapshots and the dice --- */
/* A recall (or a dice roll) never jumps: the discrete state (the scene,
 * the mutes, the arp, freeze, the LFO switch) lands at once - the scene
 * has its own ~10 s field crossfade and the mutes glide in the render -
 * while every continuous parameter rides a 1.2 s smoothstep glide,
 * stepped at frame rate. Any encoder movement cancels the glide: the
 * performer wins. */
static void scene_goto(int idx);             /* below */
static volatile uint32_t dice_count;         /* the tests read it */

static void rc_read(float *v)
{
    int i;
    v[RC_SEL] = set.sel;
    v[RC_K1] = set.k1;
    v[RC_K2] = set.k2;
    v[RC_K3] = set.k3;
    v[RC_K4] = set.k4;
    for (i = 0; i < 4; i++)
        v[RC_MIX0 + i] = set.mix[i];
    v[RC_SHIM] = set.shimmer;
    v[RC_CRUSH] = set.crush;
    v[RC_REV] = set.revecho;
    v[RC_WIDE] = set.wide;
    v[RC_FDEP] = set.flfo_dep;
    v[RC_FRATE] = set.flfo_rate;
    v[RC_ARATE] = set.arp_rate;
    v[RC_AGATE] = set.arp_gate;
}

static void rc_write(const float *v)
{
    int i;
    set.sel = (int8_t)clamp((int32_t)(v[RC_SEL] + (v[RC_SEL] >= 0.0f ? 0.5f : -0.5f)), -48, 48);
    set.k1 = (uint8_t)(v[RC_K1] + 0.5f);
    set.k2 = (uint8_t)(v[RC_K2] + 0.5f);
    set.k3 = (uint8_t)(v[RC_K3] + 0.5f);
    set.k4 = (uint8_t)(v[RC_K4] + 0.5f);
    for (i = 0; i < 4; i++)
        set.mix[i] = (uint8_t)(v[RC_MIX0 + i] + 0.5f);
    set.shimmer = (uint8_t)(v[RC_SHIM] + 0.5f);
    set.crush = (uint8_t)(v[RC_CRUSH] + 0.5f);
    set.revecho = (uint8_t)(v[RC_REV] + 0.5f);
    set.wide = (uint8_t)(v[RC_WIDE] + 0.5f);
    set.flfo_dep = (uint8_t)(v[RC_FDEP] + 0.5f);
    set.flfo_rate = (uint8_t)(v[RC_FRATE] + 0.5f);
    set.arp_rate = (uint8_t)(v[RC_ARATE] + 0.5f);
    set.arp_gate = (uint8_t)(v[RC_AGATE] + 0.5f);
}

static void rc_poll(void)
{
    float t, s, v[RC_N];
    int i;
    if (!rc.on)
        return;
    t = (float)(plat_ms() - rc.t0) / 1200.0f;
    if (t >= 1.0f) {
        rc_write(rc.to);
        rc.on = 0;
    } else {
        s = t * t * (3.0f - 2.0f * t);        /* smoothstep: soft at both ends */
        for (i = 0; i < RC_N; i++)
            v[i] = rc.from[i] + (rc.to[i] - rc.from[i]) * s;
        rc_write(v);
    }
    set_dirty = 1;
}

static void snap_capture(snap_t *d)
{
    int i;
    d->scene = set.scene;
    d->sel = set.sel;
    d->k1 = set.k1;
    d->k2 = set.k2;
    d->k3 = set.k3;
    d->k4 = set.k4;
    for (i = 0; i < 4; i++)
        d->mix[i] = set.mix[i];
    d->mute = set.mute;
    d->feat3 = (uint8_t)(set.feat & 0x07u);
    d->arp_rate = set.arp_rate;
    d->arp_gate = set.arp_gate;
    d->shimmer = set.shimmer;
    d->crush = set.crush;
    d->revecho = set.revecho;
    d->wide = set.wide;
    d->flfo = (uint8_t)(set.flfo & 1u);
    d->flfo_dep = set.flfo_dep;
    d->flfo_rate = set.flfo_rate;
    d->freeze = eng_freeze;
}

static void snap_apply(const snap_t *d)
{
    int i;
    if (d->scene != set.scene && d->scene < NSCENE)
        scene_goto(d->scene);
    set.mute = d->mute;
    set.feat = (uint8_t)((set.feat & ~0x06u) | (d->feat3 & 0x06u));
    if ((d->feat3 & 1u) && !(set.feat & 1u))
        arp_set(1);
    else if (!(d->feat3 & 1u) && (set.feat & 1u))
        arp_set(0);
    set.flfo = (uint8_t)((set.flfo & 0xFEu) | (d->flfo & 1u));
    eng_freeze = d->freeze ? 1 : 0;
    rc_read(rc.from);
    rc.to[RC_SEL] = d->sel;
    rc.to[RC_K1] = d->k1;
    rc.to[RC_K2] = d->k2;
    rc.to[RC_K3] = d->k3;
    rc.to[RC_K4] = d->k4;
    for (i = 0; i < 4; i++)
        rc.to[RC_MIX0 + i] = d->mix[i];
    rc.to[RC_SHIM] = d->shimmer;
    rc.to[RC_CRUSH] = d->crush;
    rc.to[RC_REV] = d->revecho;
    rc.to[RC_WIDE] = d->wide;
    rc.to[RC_FDEP] = d->flfo_dep;
    rc.to[RC_FRATE] = d->flfo_rate;
    rc.to[RC_ARATE] = d->arp_rate;
    rc.to[RC_AGATE] = d->arp_gate;
    rc.t0 = plat_ms();
    rc.on = 1;
    set_dirty = 1;
    ui.redraw = 1;
}

static void snap_say(const char *verb)
{
    char b[16], dg[2];
    b[0] = 0;
    text_cat(b, verb);
    text_cat(b, " ");
    dg[0] = (char)('1' + set.snap_sel);
    dg[1] = 0;
    text_cat(b, dg);
    app_say(b, "");
}

/* DICE: a full re-roll inside safe ranges - the mix never goes low
 * enough to vanish, the crush and the filter stay musical, and the
 * scene/scale system keeps everything consonant by construction. It
 * lands through the recall glide, so even a wild roll crossfades. */
static void dice_roll(void)
{
    snap_t d;
    int i;
    memset(&d, 0, sizeof d);
    d.scene = (uint8_t)(rng_next() % NSCENE);
    d.sel = (int8_t)((int)(rng_next() % 73u) - 36);
    d.k1 = (uint8_t)(30u + rng_next() % 91u);
    d.k2 = (uint8_t)(40u + rng_next() % 88u);
    d.k3 = (uint8_t)(30u + rng_next() % 91u);
    d.k4 = (uint8_t)(rng_next() % 111u);
    for (i = 0; i < 4; i++)
        d.mix[i] = (uint8_t)(70u + rng_next() % 58u);
    d.mute = 0;
    d.feat3 = (uint8_t)(set.feat & 0x07u);   /* the arp state is not the dice's business */
    d.arp_rate = set.arp_rate;
    d.arp_gate = set.arp_gate;
    d.shimmer = (uint8_t)(rng_next() % 81u);
    d.crush = (uint8_t)(rng_next() % 61u);
    d.revecho = (uint8_t)(rng_next() % 81u);
    d.wide = (uint8_t)(rng_next() % 101u);
    d.flfo = (uint8_t)(rng_next() & 1u);
    d.flfo_dep = (uint8_t)(rng_next() % 71u);
    d.flfo_rate = (uint8_t)(20u + rng_next() % 80u);
    d.freeze = eng_freeze;                   /* nor is the freeze */
    snap_apply(&d);
    dice_count++;
    app_say("DICE", SCENE[set.scene].name);
    ui.redraw = 1;
}

/* ------------------------------------------------------- v0.4: USB MIDI --- */
/* Notes snap to the scene's scale at note-on (they can never clash with
 * the drone) and play the keyboard voice with their velocity; while the
 * arp is on they join its held set instead. CCs drive the main
 * parameters directly - the number map is in the build report. */
static float midi_hz(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f); }

static int snap_midi(int note)
{
    const scene_t *sc = &SCENE[set.scene];
    int rm = 69 + (int)(12.0f * fm_log2f(sc->root / 440.0f) + 0.5f);
    int best = rm, bd = 1 << 30, o, i, cand, dd;
    for (o = -1; o < 10; o++)
        for (i = 0; i < sc->nsc; i++) {
            cand = rm + 12 * o + sc->scale[i];
            dd = cand - note;
            if (dd < 0)
                dd = -dd;
            if (dd < bd) {
                bd = dd;
                best = cand;
            }
        }
    return best;
}

static void mn_add(int note, float vel)
{
    int i;
    for (i = 0; i < mn_n; i++)
        if (mn_note[i] == note) {
            mn_vel[i] = vel;
            return;
        }
    if (mn_n < 16) {
        mn_note[mn_n] = note;
        mn_freq[mn_n] = midi_hz(note);
        mn_vel[mn_n] = vel;
        mn_n++;
    }
}

static void mn_del(int note)
{
    int i;
    for (i = 0; i < mn_n; i++)
        if (mn_note[i] == note) {
            mn_n--;
            mn_note[i] = mn_note[mn_n];
            mn_freq[i] = mn_freq[mn_n];
            mn_vel[i] = mn_vel[mn_n];
            return;
        }
}

static void midi_cc(int cc, int v)
{
    rc.on = 0;                               /* external control wins over a glide */
    switch (cc) {
    case 20: case 21: case 22: case 23:      /* the mixer: MIHYON GUWAAN AIR SIGNAL */
        set.mix[cc - 20] = (uint8_t)v;
        break;
    case 24:                                 /* SELECT: centre 64 = open */
        set.sel = (int8_t)clamp((v - 64) * 48 / 64, -48, 48);
        break;
    case 25: set.k1 = (uint8_t)v; break;
    case 26: set.k2 = (uint8_t)v; break;
    case 27: set.k3 = (uint8_t)v; break;
    case 28: set.k4 = (uint8_t)v; break;
    case 29: set.shimmer = (uint8_t)v; break;
    case 30: set.crush = (uint8_t)v; break;
    case 31: set.revecho = (uint8_t)v; break;
    case 32: set.wide = (uint8_t)v; break;
    case 33: set.flfo_dep = (uint8_t)v; break;
    case 34: set.flfo_rate = (uint8_t)v; break;
    case 35: set.arp_rate = (uint8_t)v; break;
    case 36: set.arp_gate = (uint8_t)v; break;
    case 40: case 41: case 42: case 43:      /* the layer mutes */
        if (v >= 64)
            set.mute |= (uint8_t)(1u << (cc - 40));
        else
            set.mute &= (uint8_t)~(1u << (cc - 40));
        break;
    case 44:                                 /* the filter LFO */
        set.flfo = (uint8_t)((set.flfo & 0xFEu) | (v >= 64 ? 1u : 0u));
        break;
    case 45:                                 /* FREEZE */
        eng_freeze = v >= 64 ? 1 : 0;
        ui.redraw = 1;
        break;
    case 46:                                 /* the arp */
        if ((v >= 64) != !!(set.feat & 1u))
            arp_set(v >= 64);
        break;
    default:
        return;
    }
    set_dirty = 1;
}

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if ((st & 0xF0u) == 0xB0u) {
            midi_cc((int)d1, (int)d2);
        } else if ((st & 0xF0u) == 0x90u && d2) {
            int sn = snap_midi((int)d1);
            if (set.feat & 1u)
                mn_add(sn, (float)d2 / 127.0f);
            else
                voice_start(-1 - sn, midi_hz(sn), (float)d2 / 127.0f);
        } else if ((st & 0xF0u) == 0x80u || ((st & 0xF0u) == 0x90u && !d2)) {
            int sn = snap_midi((int)d1);
            mn_del(sn);
            if (!(set.feat & 1u))
                key_off(-1 - sn);
        }
    }
}

static void scene_goto(int idx)
{
    int old = slot_cur, nw = 1 - slot_cur;
    set.scene = (uint8_t)idx;
    slot_assign(nw, 0);                      /* the new scene's first field fades in */
    slot[nw].tgt = 1.0f;
    slot[old].tgt = 0.0f;
    slot_cur = nw;
    field_left = (uint32_t)(SCENE[set.scene].field_s * SRATE);
    set_dirty = 1;
    ui.redraw = 1;
}

static void scene_change(int dir)
{
    scene_goto((set.scene + (dir > 0 ? 1 : NSCENE - 1)) % NSCENE);
    app_say("SCENE", SCENE[set.scene].name);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    int32_t e;
    keys_down = keys;
    midi_in();                               /* v0.4: USB MIDI notes and CCs */
    if (!(set.feat & 1u)) {                  /* the fingers own the keys (while the arp is off) */
        for (k = 0; k < NKEYS; k++) {
            uint32_t bit = 1u << k;
            if ((keys & bit) && !(ui.keys & bit))
                key_on((int)k);
            else if (!(keys & bit) && (ui.keys & bit))
                key_off((int)k);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (k == B_PLAY) {
            eng_playing = (uint8_t)!eng_playing;
            set.playing = eng_playing;
            set_dirty = 1;
            app_say(eng_playing ? "PLAYING" : "STOPPED", "");
        } else if (k == B_REC) {
            ui.rec_t = plat_ms();
            ui.rec_long = 0;
        } else if (k == B_SAVE) {
            save_settings();
        } else if (k == B_OCTDN) {
            ui.key_oct = clamp(ui.key_oct - 1, -1, 1);
        } else if (k == B_OCTUP) {
            ui.key_oct = clamp(ui.key_oct + 1, -1, 1);
        } else if (k == B_EDIT) {            /* v0.3: the MIXER page */
            ui.page = ui.page == 1 ? 0 : 1;
            app_say(ui.page == 1 ? "MIXER" : "MIXER OFF", "");
            ui.redraw = 1;
        } else if (k == B_FX) {              /* v0.3: the FX page - or MIHYON's mute, on the mixer page */
            if (ui.page == 1)
                mute_toggle(0);
            else {
                ui.page = ui.page == 2 ? 0 : 2;
                app_say(ui.page == 2 ? "FX" : "FX OFF", "");
                ui.redraw = 1;
            }
        } else if (k == B_SEL) {             /* the mixer page: GUWAAN's mute. The normal page: DICE (v0.4) */
            if (ui.page == 1)
                mute_toggle(1);
            else if (ui.page == 0)
                dice_roll();
        } else if (k == B_ENV) {             /* the mixer page: AIR's mute. The FX page: bank A/B (v0.4) */
            if (ui.page == 1)
                mute_toggle(2);
            else if (ui.page == 2) {
                ui.fx_bank = ui.fx_bank ? 0 : 1;
                app_say(ui.fx_bank ? "FX B" : "FX A", "");
                ui.redraw = 1;
            }
        } else if (k == B_LFO) {             /* the mixer page: SIGNAL's mute. Elsewhere: the filter LFO (v0.4) */
            if (ui.page == 1)
                mute_toggle(3);
            else {
                set.flfo = (uint8_t)(set.flfo ^ 1u);
                set_dirty = 1;
                app_say((set.flfo & 1u) ? "FLFO ON" : "FLFO OFF", "");
                ui.redraw = 1;
            }
        } else if (k == B_ARP) {             /* v0.3: tap toggles, hold cycles the pattern (the poll below) */
            ui.arp_t = plat_ms();
            ui.arp_long = 0;
        } else if (k == B_GLO) {             /* v0.3: FREEZE */
            eng_freeze = (uint8_t)!eng_freeze;
            app_say(eng_freeze ? "FREEZE" : "FREEZE OFF", "");
            ui.redraw = 1;
        } else if (k == B_HOME) {            /* tap = snapshot recall, held = the STAGE LED mode (the poll below) */
            ui.home_t = plat_ms();
            ui.home_long = 0;
        } else if (k == B_SEQ) {             /* v0.4: tap cycles the snapshot slot, held stores (the poll below) */
            ui.seq_t = plat_ms();
            ui.seq_long = 0;
        }
    }
    if (ui.rec_t) {                          /* REC long-press: hold the chord field */
        if (btn >> B_REC & 1u) {
            if (!ui.rec_long && plat_ms() - ui.rec_t >= 600u) {
                ui.rec_long = 1;
                eng_hold = (uint8_t)!eng_hold;
                app_say(eng_hold ? "HOLD" : "HOLD OFF", "");
                ui.redraw = 1;
            }
        } else {
            ui.rec_t = 0;
        }
    }
    if (ui.arp_t) {                          /* v0.3: ARP held = cycle the pattern (and switch on) */
        if (btn >> B_ARP & 1u) {
            if (!ui.arp_long && plat_ms() - ui.arp_t >= 600u) {
                int pat;
                ui.arp_long = 1;
                pat = (int)((set.feat >> 1) & 3u) + 1;
                if (pat > 2)
                    pat = 0;
                set.feat = (uint8_t)((set.feat & ~0x06u) | ((uint32_t)pat << 1));
                if (!(set.feat & 1u))
                    arp_set(1);
                set_dirty = 1;
                app_say("ARP PAT", ARP_PATN[pat]);
                ui.redraw = 1;
            }
        } else {
            if (!ui.arp_long)
                arp_set(!(set.feat & 1u));
            ui.arp_t = 0;
        }
    }
    if (ui.home_t) {                         /* HOME held = STAGE LEDs; a tap recalls the snapshot (v0.4) */
        if (btn >> B_HOME & 1u) {
            if (!ui.home_long && plat_ms() - ui.home_t >= 600u) {
                ui.home_long = 1;
                set.feat ^= 0x08u;
                set_dirty = 1;
                app_say((set.feat & 0x08u) ? "STAGE LED" : "STAGE OFF", "");
                ui.redraw = 1;
            }
        } else {
            if (!ui.home_long) {
                if (set.snap_used >> set.snap_sel & 1u) {
                    snap_apply(&set.snap[set.snap_sel]);
                    snap_say("SNAP");
                } else
                    app_say("SLOT", "EMPTY");
                ui.redraw = 1;
            }
            ui.home_t = 0;
        }
    }
    if (ui.seq_t) {                          /* v0.4: SEQ held stores into the slot; a tap cycles it */
        if (btn >> B_SEQ & 1u) {
            if (!ui.seq_long && plat_ms() - ui.seq_t >= 600u) {
                ui.seq_long = 1;
                snap_capture(&set.snap[set.snap_sel]);
                set.snap_used |= (uint8_t)(1u << set.snap_sel);
                set_dirty = 1;
                snap_say("STORE");
                ui.redraw = 1;
            }
        } else {
            if (!ui.seq_long) {
                set.snap_sel = (uint8_t)((set.snap_sel + 1) & 3);
                set_dirty = 1;
                if (set.snap_used >> set.snap_sel & 1u)
                    snap_say("SLOT");
                else {
                    char b[16], dg[2];
                    b[0] = 0;
                    text_cat(b, "SLOT ");
                    dg[0] = (char)('1' + set.snap_sel);
                    dg[1] = 0;
                    text_cat(b, dg);
                    app_say(b, "EMPTY");
                }
                ui.redraw = 1;
            }
            ui.seq_t = 0;
        }
    }
    if (set.feat & 1u) {                     /* v0.3: the arp's gate and step clocks */
        uint32_t now = plat_ms();
        if (ui.arp_cur != -1000 && (int32_t)(now - ui.arp_off_at) >= 0) {
            key_off(ui.arp_cur);
            ui.arp_cur = -1000;
        }
        if ((int32_t)(now - ui.arp_next) >= 0)
            arp_step(now);
    }
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        rc.on = 0;                           /* v0.4: a hand on a knob cancels the recall glide */
        ui.turns[k] += e;
        ui.redraw = 1;
        switch (k) {
        case EN_SELECT:                      /* the main filter's character */
            set.sel = (int8_t)clamp(set.sel + e * 4, -48, 48);
            set_dirty = 1;
            break;
        case EN_PRESET:                      /* the scene */
            scene_change(e);
            break;
        case EN_ALGO:                        /* a manual step to the next chord field */
            if (!eng_hold) {
                field_advance();
                app_say("FIELD", "");
            }
            break;
        case EN_K1:                          /* v0.3: the knobs follow the page */
        case EN_K2:
        case EN_K3:
        case EN_K4: {
            int idx = (int)k - EN_K1;
            if (ui.page == 1) {
                set.mix[idx] = (uint8_t)clamp(set.mix[idx] + e * 4, 0, 127);
                set_dirty = 1;
            } else if (ui.page == 2)
                fx_knob(idx, e);
            else
                macro_knob(idx, e);
            break;
        }
        default:
            break;
        }
    }
    rc_poll();                               /* v0.4: advance the recall glide, if one is running */
    params_from_panel();
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    lights(btn, keys);
}

void app_input_only(void) { input(); }

/* one gradient row: lerp the scene's two colours, wave-shaded, with the
 * swell's bright band drifting through */
static uint16_t grad_row(const scene_t *sc, int y, int h, uint32_t now, float swell01)
{
    float t = (float)y / (float)(h - 1);
    float w = 1.0f + 0.10f * fm_sinf((float)y * 0.11f - (float)now * 0.00045f +
                                     1.3f * fm_sinf((float)y * 0.023f + (float)now * 0.0002f));
    float band = (float)y - (110.0f - 55.0f * swell01);
    float r, g, b;
    if (band < 0.0f)
        band = -band;
    w *= 1.0f + 0.45f * fm_expf(-(band * band) / (2.0f * 18.0f * 18.0f));
    r = ((float)((sc->col_top >> 11) & 31u) * (1.0f - t) + (float)((sc->col_bot >> 11) & 31u) * t) * w;
    g = ((float)((sc->col_top >> 5) & 63u) * (1.0f - t) + (float)((sc->col_bot >> 5) & 63u) * t) * w;
    b = ((float)(sc->col_top & 31u) * (1.0f - t) + (float)(sc->col_bot & 31u) * t) * w;
    if (r > 31.0f) r = 31.0f;
    if (g > 63.0f) g = 63.0f;
    if (b > 31.0f) b = 31.0f;
    return (uint16_t)(((uint32_t)r << 11) | ((uint32_t)g << 5) | (uint32_t)b);
}

static void bar(int32_t x, int32_t y, int32_t w, uint8_t v, uint16_t col)
{
    cv_rect(x, y, w, 5, RGB(40, 40, 48));
    cv_rect(x, y, w * (int32_t)v / 127, 5, col);
}

/* ------------------------------------------------------- v0.3: the pages --- */
static void fmt_u3(char *d, int v)
{
    if (v >= 100)
        *d++ = (char)('0' + v / 100);
    if (v >= 10)
        *d++ = (char)('0' + (v / 10) % 10);
    *d++ = (char)('0' + v % 10);
    *d = 0;
}

/* one page row: a name, a bar (hollow when muted), the value at the right */
static void prow(int32_t ly, int32_t by, const char *name, uint8_t v, int muted)
{
    char num[8];
    int32_t x = cv_text(10, ly, &FONT_XS, name, muted ? RGB(200, 90, 80) : C_GRAY);
    if (muted) {
        cv_text(x + 6, ly, &FONT_XS, "MUTE", RGB(220, 70, 60));
        cv_rect(10, by, 170, 1, RGB(120, 50, 50));   /* a hollow bar: the outline only */
        cv_rect(10, by + 4, 170, 1, RGB(120, 50, 50));
        cv_rect(10, by, 1, 5, RGB(120, 50, 50));
        cv_rect(179, by, 1, 5, RGB(120, 50, 50));
    } else
        bar(10, by, 170, v, C_HI);
    fmt_u3(num, v);
    cv_text(192, ly, &FONT_XS, num, C_WHITE);
}

static void page_title(const char *t)
{
    uint32_t now = plat_ms();
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else
        cv_text(10, 6, &FONT_B, t, C_WHITE);
}

static void draw_mixer(void)
{
    cv_begin(240, 150, C_BLACK);
    page_title("MIXER");
    prow(44, 58, LAYER_NAME[0], set.mix[0], (set.mute >> 0) & 1u);
    prow(94, 108, LAYER_NAME[1], set.mix[1], (set.mute >> 1) & 1u);
    cv_blit(0, 0);
    cv_begin(240, 46, C_BLACK);
    prow(2, 13, LAYER_NAME[2], set.mix[2], (set.mute >> 2) & 1u);
    prow(24, 35, LAYER_NAME[3], set.mix[3], (set.mute >> 3) & 1u);
    cv_blit(0, 150);
    cv_begin(240, 44, C_BLACK);
    cv_text(10, 2, &FONT_XS, "KNOBS: VOLUME  MUTE: FX SEL ENV LFO", C_GRAY);
    cv_text(10, 24, &FONT_XS, "GWAAN-1 V0.4", C_GRAY);
    cv_text(178, 24, &FONT_XS, "EDIT:EXIT", C_GRAY);
    cv_blit(0, 196);
}

static void draw_fx(void)
{
    cv_begin(240, 150, C_BLACK);
    page_title(ui.fx_bank ? "FX B" : "FX A");
    if (ui.fx_bank) {                        /* v0.4, bank B */
        prow(44, 58, "REV ECHO", set.revecho, 0);
        prow(94, 108, "WIDE", set.wide, 0);
    } else {
        prow(44, 58, "SHIMMER", set.shimmer, 0);
        prow(94, 108, "CRUSH", set.crush, 0);
    }
    cv_blit(0, 0);
    cv_begin(240, 46, C_BLACK);
    if (ui.fx_bank) {
        prow(2, 13, "FLFO DEP", set.flfo_dep, 0);
        prow(24, 35, "FLFO RATE", set.flfo_rate, 0);
    } else {
        prow(2, 13, "ARP RATE", set.arp_rate, 0);
        prow(24, 35, "ARP GATE", set.arp_gate, 0);
    }
    cv_blit(0, 150);
    cv_begin(240, 44, C_BLACK);
    {
        int32_t x = cv_text(10, 2, &FONT_XS, "FREEZE", C_GRAY);
        x = cv_text(x + 4, 2, &FONT_XS, eng_freeze ? "ON" : "OFF", eng_freeze ? C_HI : C_GRAY);
        x = cv_text(x + 12, 2, &FONT_XS, "ARP", C_GRAY);
        x = cv_text(x + 4, 2, &FONT_XS, (set.feat & 1u) ? ARP_PATN[(set.feat >> 1) & 3u] : "OFF",
                    (set.feat & 1u) ? C_HI : C_GRAY);
        x = cv_text(x + 12, 2, &FONT_XS, "FLFO", C_GRAY);
        cv_text(x + 4, 2, &FONT_XS, (set.flfo & 1u) ? "ON" : "OFF", (set.flfo & 1u) ? C_HI : C_GRAY);
    }
    cv_text(10, 24, &FONT_XS, "GWAAN-1 V0.4", C_GRAY);
    cv_text(140, 24, &FONT_XS, "ENV:BANK FX:EXIT", C_GRAY);
    cv_blit(0, 196);
}

static void draw(void)
{
    const scene_t *sc = &SCENE[set.scene];
    uint32_t now = plat_ms();
    if (ui.page == 1) {                      /* v0.3: the MIXER page */
        draw_mixer();
        return;
    }
    if (ui.page == 2) {                      /* v0.3: the FX page */
        draw_fx();
        return;
    }
    float swell01 = 0.5f + 0.5f * fm_sinf(TWO_PI * swell_ph);
    int y;
    /* the slow gradient sea */
    cv_begin(240, 150, sc->col_top);
    for (y = 0; y < 150; y++)
        cv_rect(0, y, 240, 1, grad_row(sc, y, 150, now, swell01));
    cv_blit(0, 0);
    /* the scene and the state */
    cv_begin(240, 46, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(10, 8, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 8, &FONT_B, ui.msg[1], C_HI);
        cv_text(10, 32, &FONT_XS, "GWAAN-1 V0.4", C_GRAY);
    } else {
        int32_t x = cv_text(10, 8, &FONT_B, sc->name, C_WHITE);
        x = cv_text(x + 10, 8, &FONT_B, eng_hold ? "HOLD" : eng_playing ? "" : "STOPPED",
                    eng_hold ? C_HI : C_GRAY);
        /* a beacon is sounding: a quiet SIG tag while it lasts (~1.2 s) */
        if (sig_last_freq > 0.0f &&
            (uint32_t)sample_now - sig_last_at < (uint32_t)(1.2f * SRATE))
            cv_text(x + 10, 8, &FONT_B, "SIG", C_HI);
        /* v0.3/v0.4: the live states ride the footer line */
        {
            int32_t x2 = cv_text(10, 32, &FONT_XS, "GWAAN-1 V0.4", C_GRAY);
            if (eng_freeze)
                x2 = cv_text(x2 + 8, 32, &FONT_XS, "FRZ", C_HI);
            if (set.feat & 1u)
                x2 = cv_text(x2 + 8, 32, &FONT_XS, "ARP", C_HI);
            if ((set.feat >> 3) & 1u)
                x2 = cv_text(x2 + 8, 32, &FONT_XS, "STAGE", C_HI);
            if (set.flfo & 1u)
                x2 = cv_text(x2 + 8, 32, &FONT_XS, "FLFO", C_HI);
            if (set.snap_used >> set.snap_sel & 1u) {
                char sb[4];
                sb[0] = 'S';
                sb[1] = (char)('1' + set.snap_sel);
                sb[2] = 0;
                cv_text(x2 + 8, 32, &FONT_XS, sb, C_HI);
            }
        }
    }
    cv_blit(0, 150);
    /* the knob bars */
    cv_begin(240, 44, C_BLACK);
    cv_text(10, 2, &FONT_XS, "GLIDE", C_GRAY);
    bar(10, 12, 46, set.k1, C_HI);
    cv_text(66, 2, &FONT_XS, "DEPTH", C_GRAY);
    bar(66, 12, 46, set.k2, C_HI);
    cv_text(122, 2, &FONT_XS, "SPACE", C_GRAY);
    bar(122, 12, 46, set.k3, C_HI);
    cv_text(178, 2, &FONT_XS, "DRIFT", C_GRAY);
    bar(178, 12, 46, set.k4, C_HI);
    cv_text(10, 24, &FONT_XS, "FILTER", C_GRAY);
    cv_rect(66, 26, 108, 5, RGB(40, 40, 48));
    cv_rect(66 + 53, 25, 2, 7, C_GRAY);
    cv_rect(66 + 54 + (int32_t)set.sel * 54 / 48, 26, 4, 5, C_WHITE);
    cv_blit(0, 196);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.arp_cur = -1000;                      /* v0.3: nothing gated, nothing lit */
    ui.arp_led_key = -1;
    ui.arp_dir = 1;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    if (ann_on())
        ann_tick(now);
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (now - ui.last_draw >= 160u)          /* the gradient moves slowly */
        ui.redraw = 1;
    if (ui.redraw) {
        ui.redraw = 0;
        ui.last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "scene"))
        *got = set.scene;
    else if (!strcmp(what, "playing"))
        *got = eng_playing;
    else if (!strcmp(what, "hold"))
        *got = eng_hold;
    else if (!strcmp(what, "field"))
        *got = slot[slot_cur].voice;
    else if (!strcmp(what, "notes"))
        *got = (int)gen_notes;
    else if (!strcmp(what, "sig"))
        *got = (int)sig_pings;
    else if (!strcmp(what, "sigpat"))
        *got = (int)sig_pats;
    else if (!strcmp(what, "sel"))
        *got = set.sel;
    else if (!strcmp(what, "k1"))
        *got = set.k1;
    else if (!strcmp(what, "k2"))
        *got = set.k2;
    else if (!strcmp(what, "k3"))
        *got = set.k3;
    else if (!strcmp(what, "k4"))
        *got = set.k4;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "leddimn"))
        *got = popcount(led_dim_m);
    else if (!strcmp(what, "ledlit"))
        *got = popcount(led_lit_m);
    else if (!strcmp(what, "dimns"))
        *got = (int)led_ns_cur;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "mute"))
        *got = set.mute;
    else if (!strncmp(what, "mix", 3) && what[3] >= '1' && what[3] <= '4' && !what[4])
        *got = set.mix[what[3] - '1'];
    else if (!strcmp(what, "arp"))
        *got = set.feat & 1u;
    else if (!strcmp(what, "arppat"))
        *got = (set.feat >> 1) & 3u;
    else if (!strcmp(what, "arprate"))
        *got = set.arp_rate;
    else if (!strcmp(what, "arpgate"))
        *got = set.arp_gate;
    else if (!strcmp(what, "freeze"))
        *got = eng_freeze;
    else if (!strcmp(what, "shim"))
        *got = set.shimmer;
    else if (!strcmp(what, "crush"))
        *got = set.crush;
    else if (!strcmp(what, "stage"))
        *got = (set.feat >> 3) & 1u;
    else if (!strcmp(what, "ledmidn"))
        *got = popcount(led_mid_m);
    else if (!strcmp(what, "arpsteps"))
        *got = (int)arp_steps;
    else if (!strcmp(what, "arpled"))
        *got = (int)arp_last_key;
    else if (!strncmp(what, "ledseen", 7))
        *got = (int)((led_seen_m >> (atoi(what + 7) % NKEYS)) & 1u);
    else if (!strcmp(what, "snap"))
        *got = set.snap_sel;
    else if (!strcmp(what, "snapused"))
        *got = set.snap_used;
    else if (!strcmp(what, "flfo"))
        *got = set.flfo & 1u;
    else if (!strcmp(what, "flfodep"))
        *got = set.flfo_dep;
    else if (!strcmp(what, "flforate"))
        *got = set.flfo_rate;
    else if (!strcmp(what, "revecho"))
        *got = set.revecho;
    else if (!strcmp(what, "wide"))
        *got = set.wide;
    else if (!strcmp(what, "dice"))
        *got = (int)dice_count;
    else if (!strcmp(what, "midin")) {
        int i, cn = 0;
        for (i = 0; i < NKEYV; i++)
            if (keyv[i].st == 1 && keyv[i].key < 0)
                cn++;
        *got = cn;
    }
    else if (!strncmp(what, "turns", 5))
        *got = ui.turns[atoi(what + 5) % NE];
    else
        return 0;
    return 1;
}
#endif
