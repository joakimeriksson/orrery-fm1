/* SPDX-License-Identifier: GPL-3.0-only */
/* ORRERY itself (module/orrery.c, included below) played offline on the Mac: a host of the module's own table
 * with a clock that follows the audio, so a setup can be heard and measured in a second, without Docker or the
 * emulator. The module's main loop (draw) runs between 64-frame blocks; everything renders on one core.
 *
 *   render_setups SETUP SECONDS OUT.wav [PART] [SCRIPT]
 *
 * SETUP 0.. (the PRESETS turns from the first), PART -1 everyone or 0 bass 1 lead 2 strings 3 drums 4 the effects box
 * alone (the others muted with the SMK-37's mute pads). SCRIPT: "ms:event,..." before PLAY at 500 ms and after:
 * bN / bN^ a button down / up, eN:clicks an encoder, kN / kN^ a note key, mHHHHHH a MIDI message (hex). Without one
 * the setup just plays. Prints the peak and the RMS; the screen's text goes to stderr with -v as the last argument.
 * Build and run: tools/listen.py (which also measures the parts against each other). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hb_module.h"
#undef HB_DESC
#define HB_DESC const hb_module_t orrery_module
#include "module/orrery.c"

#define SR 44100u
static uint64_t frames_done;
static int verbose;
static uint32_t h_ms(void) { return (uint32_t)(frames_done * 1000u / SR); }
static uint32_t h_ticks(void) { return (uint32_t)(frames_done * 544u); }
static void h_log(const char *s) { if (verbose) fprintf(stderr, "log: %s\n", s); }
static void h_log_u32(const char *s, uint32_t v) { if (verbose) fprintf(stderr, "log: %s %08x\n", s, v); }
static void h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void h_text(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center)
{
    (void)w; (void)big; (void)fg; (void)center;
    if (verbose > 1) fprintf(stderr, "%6u text %3d,%3d %s\n", h_ms(), (int)x, (int)y, s);
}
static void h_led(uint32_t id, uint32_t on) { (void)id; (void)on; }
static uint32_t h_rand(void) { static uint32_t r = 1; r = r * 1664525u + 1013904223u; return r; }
static int h_load(void *p, uint32_t n) { (void)p; (void)n; return 1; }
static void h_save(const void *p, uint32_t n) { (void)p; (void)n; }
static int h_bank_load_n(uint32_t i, void *p, uint32_t n) { (void)i; (void)p; (void)n; return -1; }
static int h_bank_save_n(uint32_t i, const void *p, uint32_t n) { (void)i; (void)p; return (int)n; }

static void event(const char *e)
{
    int up = e[strlen(e) - 1] == '^';
    if (e[0] == 'b') orrery_module.ui((up ? HB_EV_BTN_UP : HB_EV_BTN_DOWN) | (uint32_t)atoi(e + 1), 0);
    else if (e[0] == 'k') orrery_module.ui((up ? HB_EV_KEY_UP : HB_EV_KEY_DOWN) | (uint32_t)atoi(e + 1), up ? 0 : 100);
    else if (e[0] == 'e') { const char *c = strchr(e, ':'); orrery_module.ui(HB_EV_ENC | (uint32_t)atoi(e + 1), c ? atoi(c + 1) : 1); }
    else if (e[0] == 'm') { uint32_t v = (uint32_t)strtoul(e + 1, 0, 16); orrery_module.midi(v >> 16 & 255u, v >> 8 & 255u, v & 255u); }
}
int main(int argc, char **argv)
{
    static hb_host_t host;
    static int32_t block[2 * 64];
    static struct { uint32_t ms; char e[24]; } script[256];
    uint32_t n_script = 0, at = 0, setup, seconds, k, started = 0;
    int part;
    int16_t *pcm;
    double sum = 0;
    int32_t peak = 0;
    FILE *f;
    if (argc < 4) { fprintf(stderr, "render_setups SETUP SECONDS OUT.wav [PART] [SCRIPT] [-v]\n"); return 2; }
    setup = (uint32_t)atoi(argv[1]); seconds = (uint32_t)atoi(argv[2]); part = argc > 4 ? atoi(argv[4]) : -1;
    for (k = 1; k < (uint32_t)argc; k++) if (!strcmp(argv[k], "-v")) verbose = 1; else if (!strcmp(argv[k], "-vv")) verbose = 2;
    if (argc > 5 && argv[5][0] != '-') {
        char *s = argv[5], *tok;
        for (tok = strtok(s, ","); tok && n_script < 256u; tok = strtok(0, ",")) {
            char *c = strchr(tok, ':');
            if (!c) continue;
            script[n_script].ms = (uint32_t)atoi(tok);
            snprintf(script[n_script].e, sizeof script[n_script].e, "%s", c + 1);
            n_script++;
        }
    }
    host.abi = HB_ABI;
    host.size = (uint32_t)__builtin_offsetof(hb_host_t, c1_run);   /* a host before the second core and the room */
    host.ms = h_ms; host.ticks = h_ticks; host.log = h_log; host.log_u32 = h_log_u32; host.fill = h_fill; host.text = h_text;
    host.led = h_led; host.rand = h_rand; host.load = h_load; host.save = h_save;
    host.bank_load_n = h_bank_load_n; host.bank_save_n = h_bank_save_n;
    if (orrery_module.init(&host)) { fprintf(stderr, "init failed\n"); return 1; }
    pcm = malloc(sizeof(int16_t) * 2u * SR * seconds);
    if (!pcm) return 1;
    orrery_module.draw(1);
    if (setup) orrery_module.ui(HB_EV_ENC | HB_ENC_PRESETS, (int32_t)setup);
    for (frames_done = 0; frames_done < (uint64_t)SR * seconds; frames_done += 64u) {
        uint32_t now = h_ms();
        if (!started && now >= 300u) {            /* the setup is sent: the mutes, then PLAY at half a second */
            for (k = 0; part >= 0 && k < 5u; k++) if ((int)k != part) orrery_module.midi(0x99, 36u + k, 100);
            started = 1;
        }
        if (started == 1 && now >= 500u) { if (!n_script || script[0].ms >= 500u) event("b12"); started = 2; }
        while (at < n_script && script[at].ms <= now) event(script[at++].e);
        orrery_module.draw(0);
        memset(block, 0, sizeof block);
        orrery_module.render(block, 64);
        for (k = 0; k < 128u; k++) {
            int32_t v = block[k];
            if ((v < 0 ? -v : v) > peak) peak = v < 0 ? -v : v;
            sum += (double)v * v;
            pcm[frames_done * 2u + k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    f = fopen(argv[3], "wb");
    if (!f) return 1;
    {
        uint32_t bytes = 4u * SR * seconds, h[11] = {0x46464952u, 36u + bytes, 0x45564157u, 0x20746d66u, 16u, 0x00020001u, SR, SR * 4u,
                                                     0x00100004u, 0x61746164u, bytes};
        fwrite(h, 4, 11, f);
        fwrite(pcm, 2, 2u * SR * seconds, f);
    }
    fclose(f);
    {
        double rms = 0;
        uint64_t n = (uint64_t)SR * seconds * 2u;
        rms = n ? sum / (double)n : 0;
        printf("setup %u %s part %d: peak %d rms2 %.0f clipped %u\n", setup, SETUP_NAME, part, (int)peak, rms,
               (unsigned)(snap.meters.clipped != 0));
    }
    return 0;
}
