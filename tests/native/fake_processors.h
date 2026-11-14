/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MD_NATIVE_FAKE_PROCESSORS_H
#define MD_NATIVE_FAKE_PROCESSORS_H
#include "core/instrument.h"
#include "core/effect.h"

/* Test fixtures, not production wrappers/router. Event sink receives resolved
 * physical tokens; source arbitration is intentionally left to router tests. */
#define FAKE_MAX_VOICES 4u
#define FAKE_TRACE_CAPACITY 256u
#define FAKE_PARAM_GAIN 1u
#define FAKE_INSTRUMENT_ID 0x7fff0001u
#define FAKE_EFFECT_ID 0x7fff0002u
typedef struct { md_note_token_t token; uint8_t key, velocity; } fake_voice_t;
typedef struct { uint32_t frame; md_event_t event; } fake_trace_t;
typedef struct {
    uint32_t frame, voice_count, trace_count;
    uint32_t init_calls, event_calls, render_calls, reset_calls, max_voice_count;
    uint8_t gain, voice_limit, velocity_response;
    fake_voice_t voices[FAKE_MAX_VOICES];
    fake_trace_t trace[FAKE_TRACE_CAPACITY];
} fake_instrument_state_t;
typedef struct {
    int32_t memory[2];
    uint32_t frames;
    uint16_t tempo;
    uint8_t gain;
} fake_effect_state_t;
extern const md_instrument_desc_t FAKE_INSTRUMENT; /* POLY + VELOCITY */
extern const md_instrument_desc_t FAKE_INSTRUMENT_MONO;
extern const md_instrument_desc_t FAKE_INSTRUMENT_NO_VELOCITY;
/* Counts record successful callback operations; rejected events do not mutate. */
void fake_trace_clear(fake_instrument_state_t *);
const fake_trace_t *fake_trace_at(const fake_instrument_state_t *, uint32_t index);
extern const md_effect_desc_t FAKE_EFFECT;
#endif
