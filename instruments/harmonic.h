/* SPDX-License-Identifier: GPL-3.0-only */
/* HARMONIC: an additive voice after the RMI Harmonic Synthesizer's principle (1974): two generators, each a wave of
 * sixteen harmonics at set levels (its two rows of sliders), each with an envelope of its own, the second tuned
 * against the first. That one played a key at a time; this plays six, so a run's notes ring over each other. Eight
 * sounds (tools/gen_harmonic_tables.py draws them); a sound and a level are the whole setting. */
#ifndef MD_HARMONIC_H
#define MD_HARMONIC_H
#include "core/instrument.h"
#include "adapter_common.h"
#include "builtin_ids.h"
#define MD_HARMONIC_SETTINGS_VERSION 1u
#define MD_HARMONIC_SETTINGS_BYTES 2u   /* sound, level */
#define MD_HARMONIC_SOUNDS 8u
#define MD_HARMONIC_VOICES 6u
enum { MD_HARMONIC_SOUND = 1, MD_HARMONIC_LEVEL };
typedef struct {
    md_note_token_t token;              /* the note that holds it; 0 once released */
    uint32_t phase[2], step[2];
    int32_t env[2], slope[2], goal[2];  /* a generator's level now, its change a sample, where this tick ends: Q30 */
    const int16_t *wave[2];             /* each generator's wave at this key's size */
    uint8_t bits[2];                    /* that size: 8, 7 or 6 */
    uint8_t stage[2];                   /* 0 silent, 1 attack, 2 decay, 3 release */
    uint8_t sound, key, left;           /* left: samples to the next control tick */
    int16_t gain[2];                    /* left and right: velocity and the voice's place, Q12 */
    uint32_t born;
} md_harmonic_voice_t;
typedef struct {
    uint8_t sound, level;
    uint32_t count;                     /* notes started, a voice's age */
    md_harmonic_voice_t voice[MD_HARMONIC_VOICES];
} md_harmonic_state_t;
extern const md_instrument_desc_t MD_HARMONIC;
const char *md_harmonic_name(uint32_t sound);
#endif
