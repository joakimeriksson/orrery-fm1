/* SPDX-License-Identifier: GPL-3.0-only */
#include "fake_processors.h"
#include <string.h>

static const uint32_t rates[] = {MD_SAMPLE_RATE};
static const md_param_desc_t params[] = {
    {.id=FAKE_PARAM_GAIN, .flags=MD_PARAM_AUTOMATABLE, .name="GAIN",
     .min=0, .max=127, .initial=64, .unit=MD_UNIT_INTEGER, .labels=NULL, .label_count=0}
};
static int settings_validate(const uint8_t *src, uint32_t bytes)
{
    if (!src) return MD_ERR_ARGUMENT;
    if (bytes != 1u) return MD_ERR_FORMAT;
    return src[0] <= 127u ? MD_OK : MD_ERR_RANGE;
}
static int instrument_init(void *state, uint32_t rate)
{
    if (!state) return MD_ERR_ARGUMENT;
    if (rate != MD_SAMPLE_RATE) return MD_ERR_UNSUPPORTED;
    memset(state, 0, sizeof(fake_instrument_state_t));
    ((fake_instrument_state_t *)state)->gain = 64;
    ((fake_instrument_state_t *)state)->init_calls = 1;
    ((fake_instrument_state_t *)state)->voice_limit=FAKE_MAX_VOICES;
    ((fake_instrument_state_t *)state)->velocity_response=1;
    return MD_OK;
}
static int instrument_init_mono(void *state, uint32_t rate)
{
    int status=instrument_init(state,rate);
    if (!status) ((fake_instrument_state_t *)state)->voice_limit=1;
    return status;
}
static int instrument_init_no_velocity(void *state, uint32_t rate)
{
    int status=instrument_init(state,rate);
    if (!status) ((fake_instrument_state_t *)state)->velocity_response=0;
    return status;
}
static int instrument_event(void *state, const md_event_t *event)
{
    fake_instrument_state_t *s = state;
    if (!s || !event) return MD_ERR_ARGUMENT;
    if (event->flags) return MD_ERR_UNSUPPORTED;
    if (event->kind == MD_EVENT_NOTE_ON) {
        if (!event->token || event->data.note.key > 127u || !event->data.note.velocity ||
            event->data.note.velocity > 127u) return MD_ERR_RANGE;
        for (uint32_t i=0; i<s->voice_count; i++)
            if (s->voices[i].token == event->token) return MD_ERR_STATE;
        if (s->voice_count == s->voice_limit) return MD_ERR_CAPACITY;
    } else if (event->kind == MD_EVENT_NOTE_OFF) {
        if (!event->token) return MD_ERR_RANGE;
    } else if (event->kind == MD_EVENT_PARAM) {
        if (event->data.param.id != FAKE_PARAM_GAIN) return MD_ERR_NOT_FOUND;
        if (event->data.param.value < 0 || event->data.param.value > 127) return MD_ERR_RANGE;
    } else if (event->kind != MD_EVENT_PANIC) return MD_ERR_UNSUPPORTED;
    if (s->trace_count == FAKE_TRACE_CAPACITY) return MD_ERR_CAPACITY;
    s->event_calls++;
    s->trace[s->trace_count++] = (fake_trace_t){.frame=s->frame, .event=*event};
    if (event->kind == MD_EVENT_NOTE_ON) {
        s->voices[s->voice_count++] = (fake_voice_t){event->token, event->data.note.key, s->velocity_response ? event->data.note.velocity : 100u};
        if (s->voice_count > s->max_voice_count) s->max_voice_count=s->voice_count;
    } else if (event->kind == MD_EVENT_NOTE_OFF) {
        for (uint32_t i=0; i<s->voice_count; i++) if (s->voices[i].token == event->token) {
            for (uint32_t j=i+1u; j<s->voice_count; j++) s->voices[j-1u] = s->voices[j];
            s->voices[--s->voice_count] = (fake_voice_t){0};
            break;
        }
    } else if (event->kind == MD_EVENT_PARAM) s->gain = (uint8_t)event->data.param.value;
    else { s->voice_count=0; memset(s->voices,0,sizeof s->voices); }
    return MD_OK;
}
static void instrument_render(void *state, int32_t *stereo, uint32_t frames)
{
    fake_instrument_state_t *s=state;
    uint32_t velocity=0; s->render_calls++;
    for (uint32_t v=0; v<s->voice_count; v++) velocity+=s->voices[v].velocity;
    for (uint32_t i=0; i<frames; i++) {
        int32_t sample=((int32_t)(s->frame++ & 31u)-16)*(int32_t)velocity*s->gain/127;
        stereo[2u*i]+=sample; stereo[2u*i+1u]-=sample;
    }
}
static void instrument_reset(void *state)
{
    fake_instrument_state_t *s=state; uint8_t gain=s->gain;
    uint32_t resets=s->reset_calls+1u;
    uint8_t limit=s->voice_limit, velocity=s->velocity_response;
    memset(s,0,sizeof *s); s->gain=gain; s->reset_calls=resets;
    s->voice_limit=limit; s->velocity_response=velocity;
}
static int instrument_encode(const void *state, uint8_t *dst, uint32_t capacity)
{
    if (!state || !dst) return MD_ERR_ARGUMENT;
    if (capacity < 1u) return MD_ERR_CAPACITY;
    dst[0]=((const fake_instrument_state_t *)state)->gain; return 1;
}
static int instrument_apply(void *state, const uint8_t *src, uint32_t bytes)
{
    if (!state) return MD_ERR_ARGUMENT;
    int status=settings_validate(src,bytes);
    if (status) return status;
    ((fake_instrument_state_t *)state)->gain=src[0]; return MD_OK;
}
static int instrument_get(const void *state,md_param_id_t id,int32_t *value)
{
    if(!state || !value)return MD_ERR_ARGUMENT;
    if(id!=FAKE_PARAM_GAIN)return MD_ERR_NOT_FOUND;
    *value=((const fake_instrument_state_t *)state)->gain;return MD_OK;
}
const md_instrument_desc_t FAKE_INSTRUMENT = {
    .processor={.id=FAKE_INSTRUMENT_ID,.api_version=MD_API_VERSION,.settings_version=1,.name="FAKE",
        .state_bytes=sizeof(fake_instrument_state_t),.state_alignment=_Alignof(fake_instrument_state_t),
        .sample_rates=rates,.sample_rate_count=1,.param_count=1,.params=params,.max_settings_bytes=1},
    .capabilities=MD_CAP_POLY|MD_CAP_VELOCITY,.max_polyphony=FAKE_MAX_VOICES,
    .preset_count=0,.presets=NULL,.drum_voice_count=0,.drum_voices=NULL,
    .init=instrument_init,.event=instrument_event,.get_param=instrument_get,.render_add=instrument_render,.reset=instrument_reset,
    .encode=instrument_encode,.validate=settings_validate,.apply=instrument_apply
};
const md_instrument_desc_t FAKE_INSTRUMENT_MONO = {
    .processor={.id=FAKE_INSTRUMENT_ID+1u,.api_version=MD_API_VERSION,.settings_version=1,.name="FAKE",
        .state_bytes=sizeof(fake_instrument_state_t),.state_alignment=_Alignof(fake_instrument_state_t),
        .sample_rates=rates,.sample_rate_count=1,.param_count=1,.params=params,.max_settings_bytes=1},
    .capabilities=MD_CAP_MONO|MD_CAP_VELOCITY,.max_polyphony=1,
    .preset_count=0,.presets=NULL,.drum_voice_count=0,.drum_voices=NULL,
    .init=instrument_init_mono,.event=instrument_event,.get_param=instrument_get,.render_add=instrument_render,.reset=instrument_reset,
    .encode=instrument_encode,.validate=settings_validate,.apply=instrument_apply
};
const md_instrument_desc_t FAKE_INSTRUMENT_NO_VELOCITY = {
    .processor={.id=FAKE_INSTRUMENT_ID+2u,.api_version=MD_API_VERSION,.settings_version=1,.name="FAKE",
        .state_bytes=sizeof(fake_instrument_state_t),.state_alignment=_Alignof(fake_instrument_state_t),
        .sample_rates=rates,.sample_rate_count=1,.param_count=1,.params=params,.max_settings_bytes=1},
    .capabilities=MD_CAP_POLY,.max_polyphony=FAKE_MAX_VOICES,
    .preset_count=0,.presets=NULL,.drum_voice_count=0,.drum_voices=NULL,
    .init=instrument_init_no_velocity,.event=instrument_event,.get_param=instrument_get,.render_add=instrument_render,.reset=instrument_reset,
    .encode=instrument_encode,.validate=settings_validate,.apply=instrument_apply
};
void fake_trace_clear(fake_instrument_state_t *state)
{
    state->trace_count=0; memset(state->trace,0,sizeof state->trace);
}
const fake_trace_t *fake_trace_at(const fake_instrument_state_t *state, uint32_t index)
{
    return index < state->trace_count ? &state->trace[index] : NULL;
}


/* One-sample stereo delay. Wet output excludes current dry signal; INSERT
 * intentionally replaces with the same wet delay, with no built-in mixer. */
static int effect_init(void *state, uint32_t rate)
{
    if (!state) return MD_ERR_ARGUMENT;
    if (rate != MD_SAMPLE_RATE) return MD_ERR_UNSUPPORTED;
    memset(state,0,sizeof(fake_effect_state_t)); ((fake_effect_state_t *)state)->gain=64; return MD_OK;
}
static int effect_param(void *state, md_param_id_t id, int32_t value)
{
    if (!state) return MD_ERR_ARGUMENT;
    if (id != FAKE_PARAM_GAIN) return MD_ERR_NOT_FOUND;
    if (value < 0 || value > 127) return MD_ERR_RANGE;
    ((fake_effect_state_t *)state)->gain=(uint8_t)value; return MD_OK;
}
static void effect_tempo(void *state, uint16_t bpm_q8) { ((fake_effect_state_t *)state)->tempo=bpm_q8; }
static int32_t scale_gain(int32_t value, uint8_t gain)
{
    /* Exact truncation with bounded 32-bit products, including INT32_MIN/MAX. */
    return (value/127)*(int32_t)gain + (value%127)*(int32_t)gain/127;
}
static void effect_wet(void *state, const int32_t *input, int32_t *wet, uint32_t frames)
{
    fake_effect_state_t *s=state;
    for (uint32_t i=0; i<frames; i++) for (uint32_t c=0; c<2; c++) {
        int32_t next=input[2u*i+c]; wet[2u*i+c]=scale_gain(s->memory[c],s->gain); s->memory[c]=next;
    }
    s->frames+=frames;
}
static void effect_replace(void *state, int32_t *stereo, uint32_t frames)
{
    fake_effect_state_t *s=state;
    for (uint32_t i=0; i<frames; i++) for (uint32_t c=0; c<2; c++) {
        int32_t next=stereo[2u*i+c]; stereo[2u*i+c]=scale_gain(s->memory[c],s->gain); s->memory[c]=next;
    }
    s->frames+=frames;
}
static void effect_reset(void *state)
{
    fake_effect_state_t *s=state; uint8_t gain=s->gain; uint16_t tempo=s->tempo;
    memset(s,0,sizeof *s); s->gain=gain;s->tempo=tempo;
}
static int effect_encode(const void *state, uint8_t *dst, uint32_t capacity)
{
    if (!state || !dst) return MD_ERR_ARGUMENT;
    if (!capacity) return MD_ERR_CAPACITY;
    dst[0]=((const fake_effect_state_t *)state)->gain; return 1;
}
static int effect_apply(void *state, const uint8_t *src, uint32_t bytes)
{
    if (!state) return MD_ERR_ARGUMENT;
    int status=settings_validate(src,bytes); if (status) return status;
    ((fake_effect_state_t *)state)->gain=src[0]; return MD_OK;
}
static int effect_get(const void *state,md_param_id_t id,int32_t *value)
{
    if(!state || !value)return MD_ERR_ARGUMENT;
    if(id!=FAKE_PARAM_GAIN)return MD_ERR_NOT_FOUND;
    *value=((const fake_effect_state_t *)state)->gain;return MD_OK;
}
const md_effect_desc_t FAKE_EFFECT = {
    .processor={.id=FAKE_EFFECT_ID,.api_version=MD_API_VERSION,.settings_version=1,.name="FAKE DELAY",
        .state_bytes=sizeof(fake_effect_state_t),.state_alignment=_Alignof(fake_effect_state_t),
        .sample_rates=rates,.sample_rate_count=1,.param_count=1,.params=params,.max_settings_bytes=1},
    .capabilities=MD_EFFECT_INSERT|MD_EFFECT_BUS,.init=effect_init,.set_param=effect_param,.get_param=effect_get,.set_tempo=effect_tempo,
    .process_replace=effect_replace,.process_wet=effect_wet,.reset=effect_reset,
    .encode=effect_encode,.validate=settings_validate,.apply=effect_apply
};
