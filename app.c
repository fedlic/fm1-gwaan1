/* GWAAN-1: an ambient drone synth (spec v0.1, v0.2 additions). The app's own
 * code (FEDLIC TOKYO); the platform is fm1-base (MPL-2.0). Part of the unity
 * build: gfx.c and announce.c are in scope.
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
typedef struct {
    uint32_t magic;
    uint8_t scene, playing, rsv0, rsv1;
    int8_t sel;                          /* SELECT detents, -48..48 */
    uint8_t k1, k2, k3, k4;              /* the knob macros, 0..127 */
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.scene = SC_DEEP;
    set.playing = 1;
    set.rsv0 = 0;
    set.rsv1 = 0;
    set.sel = 0;
    set.k1 = 64;                         /* glide */
    set.k2 = 64;                         /* depth */
    set.k3 = 70;                         /* space */
    set.k4 = 40;                         /* drift */
}
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.scene >= NSCENE || set.playing > 1u || set.sel < -48 || set.sel > 48 ||
        set.k1 > 127u || set.k2 > 127u || set.k3 > 127u || set.k4 > 127u)
        settings_defaults();
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
} TGT;
static struct {                          /* the render's own smoothed copies */
    float sel, lim;
    float drift_cut, drift_det, drift_gld;
    float body, detune;                  /* K2's fatness, glided so turns never step */
} SM;

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
    int8_t key;                          /* the panel key it belongs to */
    float ph, mph, freq, level;
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
    const scene_t *sc = &SCENE[set.scene];
    schedule();
    for (i = 0; i < n; i++) {
        float s = 0.0f, ks = 0.0f, out, wet, echo_out, rev = 0.0f;
        int v, c, sl, j;
        /* glide the SELECT character: no zipper noise */
        SM.sel += (TGT.sel - SM.sel) * 0.0012f;
        SM.body += (TGT.body - SM.body) * 0.0006f;
        SM.detune += (TGT.detune - SM.detune) * 0.0008f;
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
            s += fm_sinf(TWO_PI * slot[0].ph[0][0]) * sub_g * (slot[0].g + slot[1].g > 0.01f ? 1.0f : 0.0f);
            /* a fifth above, much quieter, for body */
            slot[0].ph[0][1] += sub_freq * 1.5f / SRATE;
            if (slot[0].ph[0][1] >= 1.0f)
                slot[0].ph[0][1] -= 1.0f;
            s += fm_sinf(TWO_PI * slot[0].ph[0][1]) * sub_g * 0.35f;
            /* the sub's 2nd harmonic (v0.2): weight a headphone can hear */
            slot[0].ph[0][2] += sub_freq * 2.0f / SRATE;
            if (slot[0].ph[0][2] >= 1.0f)
                slot[0].ph[0][2] -= 1.0f;
            s += fm_sinf(TWO_PI * slot[0].ph[0][2]) * sub_g * 0.30f;
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
                s += (fm_sinf(bp) + 0.25f * fm_sinf(2.0f * bp)) * bg;
                s += fm_sinf(TWO_PI * body3_ph) * bg * 0.55f;   /* root x3: the fifth's body */
                /* v0.2.1: root x2 (147-262 Hz across the scenes) - the
                 * low-mid warmth that makes KNOB2's instant component
                 * obvious on the built-in speaker within a second */
                s += fm_sinf(TWO_PI * body2_ph) * bg * 0.85f;
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
                s += (fm_sinf(TWO_PI * slot[sl].ph[j][0]) + fm_sinf(TWO_PI * slot[sl].ph[j][1]) +
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
            s += fm_sinf(TWO_PI * m->ph + 2.6f * env * fm_sinf(TWO_PI * m->mph)) * env * m->peak * ans_amp;
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
            tgt = kv->st == 1 ? 0.17f : 0.0f;
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
            s += bp * sc->air * 0.16f * (0.7f + 0.3f * fm_sinf(TWO_PI * air_am_ph));
            s += (nz - air_lp) * TGT.shimmer * 0.028f;
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
            s += fm_sinf(TWO_PI * d->ph) * env * d->level *
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
            s += fm_sinf(TWO_PI * p->ph) * env * p->peak;
        }

        /* PLAY stops the generation (a ~0.3 s fade); the keys still play */
        eng_gain += ((eng_playing ? 1.0f : 0.0f) - eng_gain) * 0.001f;
        s = (s * eng_gain + ks) * 0.5f;   /* headroom: the wash stays out of saturation */

        /* the SELECT main filter over the whole scene */
        out = main_filter(s, SM.sel, SM.drift_cut);

        /* the space wash (K3): tape echo, capped feedback, degrading repeats */
        echo_out = echo_buf[echo_pos];
        echo_lp += 0.38f * (echo_out - echo_lp);
        echo_buf[echo_pos] = out * TGT.echo_send + echo_lp * (TGT.echo_fb > 0.82f ? 0.82f : TGT.echo_fb);
        echo_pos = (echo_pos + 1u) % ECHO_LEN;
        wet = out + echo_lp * 0.5f;
        /* reverb: 4 combs + 2 allpasses (Schroeder) */
        if (TGT.rev_send > 0.01f) {
            float rin = wet * TGT.rev_send * 0.42f;
            for (c = 0; c < 4; c++) {
                uint32_t p = comb_pos[c];
                float y = comb_buf[c][p];
                comb_buf[c][p] = rin + y * 0.62f;
                comb_pos[c] = (p + 1u) % COMB_LEN[c];
                rev += y;
            }
            rev *= 0.25f;
            for (c = 0; c < 2; c++) {
                uint32_t p = ap_pos[c];
                float y = ap_buf[c][p];
                float x = rev + y * 0.5f;
                ap_buf[c][p] = x;
                ap_pos[c] = (p + 1u) % AP_LEN[c];
                rev = y - x * 0.5f;
            }
            wet += rev * 0.5f;
        }

        /* ceiling: gentle saturation, then a limiter that only ever turns
         * down. The ceiling is 0.55, not 0.92: the gain staging below
         * multiplies by 1.6, so 0.55 lands at ~-1.7 dBFS at the output. */
        out = fm_tanhf(wet * 0.9f);
        {
            float m = fm_fabsf(out), gl2;
            if (m * SM.lim > 0.55f)
                SM.lim = 0.55f / m;
            else if (SM.lim < 1.0f)
                SM.lim += (1.0f - SM.lim) * 0.0004f;
            gl2 = out * SM.lim * gain_now;
            lr[2 * i] = (int32_t)gl2;
            lr[2 * i + 1] = (int32_t)gl2;
        }
        sample_now++;
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
} ui;

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
static void lights(uint32_t btn, uint32_t keys)
{
    uint32_t lit = 0, dim = (1u << NKEYS) - 1u, b, db;
    uint32_t now = plat_ms();
    int32_t ns;
    int k;
    for (k = 0; k < NKEYS; k++)
        if (keys >> k & 1u)
            lit |= 1u << k;
    b = btn | (set_dirty ? 1u << B_SAVE : 0u) | (eng_hold ? 1u << B_REC : 0u);
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
    plat_lights(b, lit, db, dim, 1);
    /* a sounding beacon lights its nearest key at mid level for ~1.2 s -
     * the transmitter blinking out there, answered on the keyboard */
    {
        uint32_t mid = 0;
        if (sig_last_freq > 0.0f &&
            (uint32_t)sample_now - sig_last_at < (uint32_t)(1.2f * SRATE)) {
            int bk = -1, k2;
            float bd = 1e9f;
            for (k2 = 0; k2 < NKEYS; k2++) {
                float r = sig_last_freq / key_freq(k2);
                float d = r > 1.0f ? r : 1.0f / r;
                if (d < bd) {
                    bd = d;
                    bk = k2;
                }
            }
            if (bk >= 0)
                mid = 1u << bk;
        }
        plat_lights_mid(0, mid);
    }
    led_dim_m = dim;
    led_lit_m = lit;
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

static void key_on(int k)
{
    int i, best = -1;
    for (i = 0; i < NKEYV; i++)
        if (keyv[i].st == 1 && keyv[i].key == k)
            return;                          /* already sounding */
    for (i = 0; i < NKEYV; i++)
        if (!keyv[i].st) {
            best = i;
            break;
        }
    if (best < 0) {                          /* steal the quietest releasing voice */
        float bl = 1e9f;
        for (i = 0; i < NKEYV; i++)
            if (keyv[i].level < bl) {
                bl = keyv[i].level;
                best = i;
            }
    }
    keyv[best].key = (int8_t)k;
    keyv[best].freq = key_freq(k);
    keyv[best].ph = 0.0f;
    keyv[best].mph = 0.0f;
    keyv[best].st = 1;
}

static void key_off(int k)
{
    int i;
    for (i = 0; i < NKEYV; i++)
        if (keyv[i].st == 1 && keyv[i].key == k)
            keyv[i].st = 2;                  /* melt: the render's slow release */
}

static void scene_change(int dir)
{
    int old = slot_cur, nw = 1 - slot_cur;
    set.scene = (uint8_t)((set.scene + (dir > 0 ? 1 : NSCENE - 1)) % NSCENE);
    slot_assign(nw, 0);                      /* the new scene's first field fades in */
    slot[nw].tgt = 1.0f;
    slot[old].tgt = 0.0f;
    slot_cur = nw;
    field_left = (uint32_t)(SCENE[set.scene].field_s * SRATE);
    set_dirty = 1;
    app_say("SCENE", SCENE[set.scene].name);
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    int32_t e;
    keys_down = keys;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_on((int)k);
        else if (!(keys & bit) && (ui.keys & bit))
            key_off((int)k);
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
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
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
        case EN_K1:
            set.k1 = (uint8_t)clamp(set.k1 + e * 4, 0, 127);
            set_dirty = 1;
            break;
        case EN_K2:
            set.k2 = (uint8_t)clamp(set.k2 + e * 4, 0, 127);
            set_dirty = 1;
            break;
        case EN_K3:
            set.k3 = (uint8_t)clamp(set.k3 + e * 4, 0, 127);
            set_dirty = 1;
            break;
        case EN_K4:
            set.k4 = (uint8_t)clamp(set.k4 + e * 4, 0, 127);
            set_dirty = 1;
            break;
        default:
            break;
        }
    }
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

static void draw(void)
{
    const scene_t *sc = &SCENE[set.scene];
    uint32_t now = plat_ms();
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
        cv_text(10, 32, &FONT_XS, "GWAAN-1 V0.2.1", C_GRAY);
    } else {
        int32_t x = cv_text(10, 8, &FONT_B, sc->name, C_WHITE);
        x = cv_text(x + 10, 8, &FONT_B, eng_hold ? "HOLD" : eng_playing ? "" : "STOPPED",
                    eng_hold ? C_HI : C_GRAY);
        /* a beacon is sounding: a quiet SIG tag while it lasts (~1.2 s) */
        if (sig_last_freq > 0.0f &&
            (uint32_t)sample_now - sig_last_at < (uint32_t)(1.2f * SRATE))
            cv_text(x + 10, 8, &FONT_B, "SIG", C_HI);
        cv_text(10, 32, &FONT_XS, "GWAAN-1 V0.2.1", C_GRAY);
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
    else if (!strncmp(what, "turns", 5))
        *got = ui.turns[atoi(what + 5) % NE];
    else
        return 0;
    return 1;
}
#endif
