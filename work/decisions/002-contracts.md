# Decision 002: portable source contracts and project layout

Task: M1-01. Owner: agent-a. Status: proposed for agent-b review. Date: 2026-11-05.
Depends on accepted baseline and sequencer reuse decision 001. This freezes interfaces, not their implementation.

## Portable boundary

The C11 headers in core/param.h, event.h, instrument.h, effect.h and project_format.h include only stdint.h
and each other. No hb_module.h, platform, engine or allocator dependency. Runtime structs are a source API, not a
binary ABI. Registry contexts are explicit (an improvement on the earlier global find sketch); IDs remain stable
regardless of registry order. Descriptors/presets are immutable; caller-owned state is aligned to a declared
power of two. The runtime rejects duplicate registry/parameter/preset IDs, invalid ranges/defaults/labels,
missing required callbacks, unsupported API versions/rates, insufficient storage and bad alignment before init.
An instance cannot share state with another instance; caller lifetime and registry lifetime are explicit.

Reserve instrument IDs 0x00010001 ARP2600, 0x00010002 Eminent, 0x00010003 MiniPops, 0x00010004 Electronic FX;
effect IDs 0x00020001 Filter, 0x00020002 Phaser, 0x00020003 Echo, 0x00020004 Reverb. Zero is invalid/bypass,
not an instrument. Adapter catalog headers will expose these IDs. Descriptor API version is 1; each processor
owns an independent settings version. Presets carry stable IDs and validated raw settings of that version.
No API promises Melodee preset/project compatibility.

Exactly one of MONO/POLY is set for pitched instruments; drum-only instruments may set neither but must set
PERCUSSION. max_polyphony includes release voices and is nonzero. MONO means one sounding pitched voice;
POLY may still play a monophonic pattern. Declaring percussion does not imply pitched note support. Capabilities
must describe actual behavior: current ARP has no velocity response or explicit accent/slide until M1-05.
Unsupported explicit features fail without mutating DSP, not by silently pretending they worked.

Percussion descriptors include kit voice IDs/names and MIDI note mapping (0..127, or 255 unmapped). v1 mask IDs
are 0..13 with no duplicate IDs; count/pointer must be consistent and nonempty for PERCUSSION, NULL/zero otherwise.
Unused IDs in a kit are not valid hits. Multiple kit IDs may share a MIDI note only with an explicit input mapping
policy; default mappings should be unique. Names and IDs enable generic kit editing without MiniPops branches.

The router rejects POLY admission that would exceed max_polyphony distinct held pitches; same-pitch logical
owners do not consume another physical slot. MONO keeps multiple logical owners and resolves one priority voice,
so max_polyphony=1 does not prevent live override/resume. Engines may steal released tails to accept new held
voices. Self-terminating HITs may retrigger/steal transient voices according to a documented kit policy, without
evicting unrelated held pitched owners. A hybrid HIT may return MD_ERR_CAPACITY without mutation when all
physical slots are held. The router counts/returns that admission failure without a track flush; other unexpected
backend failures retain fail-safe reset/flush behavior. The global logical-note pool still bounds mono held-key history.

Parameters have local IDs 1..4095 in 16-bit storage, native signed 32-bit range/default, unit, contiguous enum
labels when flagged and smoothing/automation flags. Validate enum count == max-min+1 with bounded arithmetic;
read-only and automated flags cannot both be set. Non-enum labels/count must be NULL/0. Logarithmic DSP/display
mapping remains adapter-specific and documented; these headers do not force linear Hertz modulation. UI/model
snapshots expose current base values; they do not query or serialize DSP from a drawing callback.

Encode returns nonnegative payload length or a negative error. validate checks the entire versioned payload
without mutation. Apply calls validate first; after successful validation it must succeed for the same payload.
Sound application occurs stopped or at a bounded safe handoff after appropriate source release. Runtime engine
state, pointers, structs and history never appear in sound payloads. Required reset retains parameters but clears
voices and history. Descriptor/settings changes are versioned; field offsets are never inferred from sizeof.

## Events and ownership

md_event_t is an untimestamped, pointer-free logical command. md_timed_event_t supplies a block offset; router
and session apply it before the sample at that offset. An event at offset n belongs to the next block. No processor
interprets timestamps or drives transport. Every admitted pitched note-on has a nonzero token unique among
outstanding logical notes; wrap skips live tokens. The input layer retains key/channel-to-token mapping so
note-off is not remapped by a later octave/page/channel change. Stale logical offs are successful no-ops.

The router owns the logical source/token book. It resolves duplicate pitches and mono priority before processor
callbacks, which receive physical voice events. If an engine only represents one voice per pitch, the router keeps
that physical token until the last logical owner releases. Removing the first owner cannot invalidate the voice
token still held by others. Mono arbitration prefers the most recent live/MIDI note over sequenced notes; when
live input releases, restore the surviving sequenced note. Do not keep a second independent logical note stack
inside an adapter. An engine's internal held-key helper may be used only as an implementation detail of resolved
voice transitions and must not leak additional owners.

NOTE_ON key/velocity ranges are 0..127/1..127; NOTE_OFF uses token, not an input pitch lookup. HIT has a stable
kit voice ID and velocity 1..127, is self-terminating, and need not have a token. Hit events do not create logical
held-note entries; mute blocks future hits and the mixer silences that track while shared tails may ring.
PARAM uses the target track and local processor parameter ID; the session decodes automation namespaces before
dispatch. BEND is signed Q16 semitones in [-48,+48]; PRESSURE is 0..127. SOURCE_RELEASE is router-only and
expands the specified source (one track or all); sustain cannot prevent this release. PANIC resets every addressed
instrument and all effect histories when global. Normal stop releases sequence sources and preserves live input/tails.
All unused fields are initialized to zero. Reserved sources and invalid track/kind/flag/value combinations fail
before mutation. MD_SOURCE_NONE is permitted only for PARAM and PANIC from UI/system operations. Other kinds
require a declared panel/MIDI/sequence source; reserved IDs remain invalid. Unsupported per-note pitch pressure
is not introduced by this API; pressure is channel/source
expression routed to the instance.

Sources 0..3 are sequencer tracks; 4 is panel; 8..55 represent up to three MIDI ports times sixteen channels.
Other IDs are reserved. Limits start at 128 logical notes, 128 queued commands (32 release/stop reserve), 64 source
IDs and 512 dispatched events per 256-frame block. Session tests must prove admission behavior within these
limits. Reserve is supplemented by a coalesced emergency source-flush flag; it does not promise every note-off
fits an otherwise saturated queue. Invalid/excess new notes are rejected visibly. No lost release, unbounded
catch-up or waiting in audio is permitted. Change limits only with measured RAM/CPU and contract review.

## Effect instances and fixed routing

Effects share descriptor/parameter/settings conventions, not note ownership. INSERT requires process_replace;
BUS requires process_wet. A descriptor can support both if both callbacks exist. Required init/reset/settings/
parameter callbacks must exist. Tempo is optional; set_tempo receives 40..240 BPM Q8 on change. There is no
second effect clock. Insert buffers are explicitly in-place; bus input/output must not overlap, and output is
wet-only replacement, never dry-plus-wet or additive. Every processor supports 1..256 frame spans.

Mixer owns bypass ramps, gains, routing and whether to keep feeding a bypassed processor to preserve continuity;
no dormant state may return as an unexpected stale echo. Muted tracks or absent input feed zero into sends;
existing returns drain normally. STOP releases only sequenced sources, so held live/audition notes continue to
feed sends. Panic/project replacement resets effect history. Per-track sends are post-insert, post-level/pan;
echo and reverb returns do not feed each other. Master sweep follows dry+returns. Existing Eminent ensemble/phaser
stay inside that instrument initially, not simultaneously enabled again as default inserts. Host room is requested
off outside audio where supported; forced host processing is reported separately.

All signals are interleaved stereo Q15 in int32_t buffers, not necessarily clamped to int16_t between processors.
Instrument render is additive; effect render replaces its output. Internal accumulators must have documented
headroom and avoid signed overflow. Mixer/output saturation and gain staging are verified in M3, not assumed
from the old baseline. Optional slots do not imply every combination fits CPU/RAM/settings pools.

## Automation target map

A lock stores its owning step (track implied) plus a 16-bit target and signed 16-bit native value. Descriptor
parameter IDs occupy the low twelve bits; the high nibble selects a session destination:

| High nibble | Destination |
| --- | --- |
| 0x0 | Instrument on owning track |
| 0x1 / 0x2 | Insert 1 / 2 on owning track |
| 0x3 | Track mixer: 1 level, 2 pan, 3 echo send, 4 reverb send |
| 0x4 / 0x5 / 0x6 | Shared echo / shared reverb / master filter |
| 0x7..0xf | Reserved, rejected in v1 |

Local ID zero is invalid. Only declared automatable parameters fitting int16 native values can be locked.
Header fields designate one owning track per global destination or 255 (manual only). Conflicting global locks
are rejected at edit/load, not resolved by iteration order. Instrument/chain replacement must validate or explicitly
remove affected locks as part of one undoable edit. Lock lifetime is one visited step; restore base on the next
step, including rest/probability skips, before applying another lock. UI base edits remain underneath the lock.

## Exact v1 wire regions

All multibyte values are explicit little endian. Fixed region allocation gives maximum **8096 bytes**, leaving
80 bytes in the host's 8176-byte bank. The trailing bank space is not part of this version's payload. Codec
implementation is M5-01; the following offsets are the input contract for earlier model/record/UI work.

| Region | Absolute start | Bytes |
| --- | ---: | ---: |
| Header | 0 | 128 |
| 4 track records | 128 | 128 |
| 16 pattern metadata | 256 | 128 |
| 1024 steps | 384 | 6144 |
| 32 sections | 6528 | 512 |
| 48 locks | 7040 | 288 |
| Instrument sound pool | 7328 | 512 |
| Effects directory and payload pool | 7840 | 256 |
| End | 8096 | — |

Header layout (offsets relative to zero): magic ASCII MDP1 [0..3], schema u16 [4], header length u16 [6], total
length u32 [8], CRC-32 u32 [12], UTF-8 project name bytes[24] [16], BPM Q8 u16 [40], song count u8 [42], loop
u8 [43], deterministic seed u32 [44], used lock count u8 [48], echo/reverb/master automation owners u8[3] [49],
reserved zero bytes[76] [52]. Name is NUL-terminated, at most 23 UTF-8 bytes without embedded malformed sequences;
unused name bytes are zero. CRC is IEEE CRC-32, reflected polynomial 0xedb88320, initial/final XOR 0xffffffff,
over all 8096 bytes with the CRC field treated as zero. Header/schema/length mismatch rejects before CRC/model load.
Owners are track 0..3 or 255; count 0 means pattern-only project, 1..32 means available song sections. No playback
position/autoplay or physical MIDI-clock source preference is stored in a project.

Each 32-byte track: instrument ID u32 [0], sound-pool-relative offset u16 [4], sound payload length u16 [6], sound
version u16 [8], selected pattern u8 [10], MIDI input channel u8 [11] (0..15 or 255 disabled), level Q15 u16 [12],
pan signed Q15 i16 [14], echo/reverb sends Q15 u16 [16/18], flags u8 [20] (bit0 mute, bit1 solo), input flags u8
[21] (bit0 panel, bit1 MIDI), panel octave offset i8 [22] (-4..4), record mode u8 [23] (0 step, 1 overdub, 2 replace),
reserved zero bytes[8] [24]. Q15 gains are 0..32767, pan -32767..32767. At most one track has panel input enabled;
MIDI channel may fan out intentionally. No instrument ID zero in v1. Sound offset+length must fit the 512-byte pool;
nonempty payloads are disjoint. Zero-length settings are allowed only when that descriptor accepts them.
All unused pool bytes are zero. Settings belong to a track even when two instruments have identical sounds.

Each 8-byte pattern metadata: length u8, division u8, swing u8, transpose i8, mode u8, flags u8, reserved u16.
Length 1..64; division enum order 1/32,1/16,1/8,1/4,1/2,bar; swing 50..75; transpose -48..48 (zero for drums);
mode mono/chord/drum; flags/reserved zero. Serialize patterns track-major then pattern-major. Step storage is
fixed at 64 per pattern; inactive steps remain editable valid records rather than arbitrary unvalidated bytes.
Each 6-byte step: payload u16, velocity u8, gate u8, flags u8, probability u8. Mono payload is 0..127; chord payload
is root (0..127) in low byte, quality enum in high byte. Drum payload's upper two bits must be zero. Probability
0..100. Gate zero rest; otherwise 1..200% straight duration. Nonrest velocity 1..127; rest velocity may be 0..127.
TIE on a pitched nonrest step extends prior pitches (payload ignored), including across loop boundaries; it does
not change pitch. SLIDE on a new monophonic note approaches that note legato from an existing predecessor, or plays
normally if none survives (no slide flag emitted); chord slide is rejected in v1; explicit capability required. Drum TIE/SLIDE flags are rejected, gate duration ignored
except zero rest. Undefined flag bits rejected. Mono onsets replace prior sequence note; chord gates may overlap
within bounded ownership pools. Out-of-range transposed pitches are not wrapped.

Each 16-byte section: pattern[4] u8 (0..3,254 HOLD,255 REST), bars u8 1..64, transpose i8 -48..48, mute mask u8
low four bits, flags u8 zero, reserved bytes[8] zero. Inactive section slots are all zero. Each 6-byte lock: step
reference u16 ((track*4+pattern)*64+step), namespaced target u16, value i16. Store used locks in strictly increasing
step order, at most one lock per step; unused lock records are all zero. Pattern counts, flags, owners, capabilities
and referenced instances are all validated before publication. Gate duration is derived from note releases by the
recorder; recordings requiring a gate above 200% or unsupported chord must report the limit rather than truncate
silently. Count-in and UI cursor preferences live outside project wire data in the <=240-byte settings record.

The 256-byte effect region begins with **eleven fixed 12-byte directory records** (132 bytes), then 124 payload
bytes. Directory index track*2+slot addresses eight inserts; 8 echo, 9 reverb, 10 master filter. Record: processor
ID u32 [0], settings version u16 [4], flags u16 [6] (bit0 bypass), payload offset u16 [8], payload length u16 [10].
Offsets are relative to the 124-byte payload subregion, not whole region. ID zero means empty and all record bytes
zero; no payload. Nonempty records must have proper insert/bus capability and disjoint payload ranges. All unused
bytes are zero. Empty master/return slots are valid dry/bypass configurations. This fits eight 12-byte insert
payloads plus three 8-byte bus/master payloads (120 bytes); it does not promise arbitrary future presets fit.
Reject sound/chain edits that cannot serialize before publishing them; never discover that failure at SAVE.

## Reuse and verification

Decision 001 supports selective reuse. No copied Melodee function is included in these headers. M2-01 may adapt
its exact remainder approach with pinned provenance while preserving our tick/event interface. Whole-stack reuse
would reopen that decision, not bypass contract checks. New C code retains GPL-3.0-only notices consistent with
the imported engines. Native checks for this task compile all headers together at C11 strict warning levels and
check fixed region/record sizes plus an independent layout arithmetic oracle. Runtime registry/event/processor
behavior is implemented and tested in subsequent tasks; declarations alone are not an implemented API.

M2-03 scheduling clarification: the sequencer emits slide NOTE_ON before releasing its predecessor at the same
sample, tagged as a final cleanup priority. All ordinary note-offs still precede new onsets. SLIDE does not
require a separate LEGATO capability; its own capability covers the transition. Length shrink clamps a hidden
cursor before reading the next step; no bank mutation is allowed during an active iterator.
