# orrery-fm1 / meloDock: implementation plan

Revised 2026-11-05. This is the design and delivery specification. Portable foundations now have native
implementations; WORK.json records reviewed completion. Device integration and the full workflow remain gated.

Execution is tracked in [WORK.json](WORK.json); use `python3 tools/work.py status` for current owners and ready
tasks. [EXECUTION.md](EXECUTION.md) defines task deliverables, dependencies, review gates and the two-agent
workflow. [AGENTS.md](AGENTS.md) is the session startup/resume protocol. Update task evidence as work happens;
this design document is not the progress ledger.

## Outcome and boundaries

Build a playable four-part workstation with a reusable instrument API, one sequencer, editable drums, song
arrangement, live recording, recallable sounds and safe project storage. ORRERY (SEVENTIES until 2026-11-15) is the first instrument
set: two ARP 2600 instances, Eminent strings and MiniPops percussion. Use **meloDock** as the working name for
the composition layer; the module identity is ORRERY (the owner's rename of 2026-11-15, before any release: the dock finds settings and
banks by a module's name, so what a unit saved as SEVENTIES stays under that name, unread).

The value is composing independently developed instruments inside fm1-dock. A new instrument should require
an adapter, parameter descriptions and presets, without changing the clock, recorder, song player or storage
infrastructure. Four engine-specific branches behind a sequencer do not meet that goal.

One `.dock` module contains the workstation and statically linked instruments. The dock still loads one module
at a time. Existing `.dock` modules cannot become concurrent instruments merely by implementing this API:
reusing Scintilla, for example, requires adapting its engine. Dynamic loading, extra cores, additional tracks
and arbitrary routing graphs are separate proposals, justified by measurement after the basic workstation works.

| Layer | Owns | Must not own |
| --- | --- | --- |
| fm1-dock | hardware, audio callback, lifecycle, MIDI I/O, flash services, device EQ/protection and optional global room | song logic and project effects |
| meloDock core | registry, transport, patterns/song, recording, routing, mix, project effects, schema | hardware registers, USB stack, engine DSP internals |
| Instrument adapters | instance state, note/voice policy, parameter conversion, sound serialization | transport, project storage, panel scanning |
| ORRERY UI/content | panel mapping, sound pages, presets and demo projects | another clock or direct writes to live DSP |

## Evidence and corrections

The first integrated build was reported as 23.8 KB code, 44.8 KB state, no RAM code; band rendering 35% of a
block, 51% with lead, no underruns over 58 s. Strings alone were 13%, bass 6%, drums 5%. These historical
baselines do not establish the cost of event splitting, automation, full polyphony or more effects.

Today's `module/orrery.c` owns engine globals, mixes four buffers, runs the bass ARP and MiniPops internal
sequencers, and changes chords in the main loop by observing the bass link. It has neither a general instrument
API nor an independent multitrack sequencer. Preserve this build as an audio reference while replacing its wiring.

Checks against this source and `../fm1-harbor-proc` establish the following constraints:

- `arp_note_on()` ignores velocity and has no explicit live accent/slide argument. Encoding accent as velocity
  >100 does not work. Add a supported entry point upstream, test it there, then copy it here.
- `mp_trigger()` already supports individual drum hits in render context. Merely changing MiniPops rhythm keys
  does not produce an editable drum sequencer; use its rhythms as pattern generators instead.
- `hb_host_t` is a module/hardware service API, not an instrument API. Render adds interleaved stereo Q15 into
  `int32_t` buffers. Module data/BSS allowance is 272 KiB; RAM code allowance is 16 KiB.
- The primary bank holds 8176 bytes. Indexed banks exist on newer hosts but share finite capacity. Bank saves
  erase flash and stall sound (~100 ms according to the ABI); moving saves outside render does not remove that stall.
- DIN real-time bytes are discarded in `firmware/src/dock.c:din_get`. The MIDI callback has no timestamp and
  documents channel messages. Clock support needs a host contract and tests, not a promised six-line fix.
- The old four-byte step cannot hold all the fields it advertised. Prove the serialized layout before promising
  capacity. A fourth track already exists: the intended four are bass, lead, strings/chords and drums.

Engine copies stay managed by `take.py`: fix shared DSP in fm1-harbor, retain its standalone modules and checks,
then import it. Adapters belong here; silently diverging engine copies do not.

## Instrument API first

Implement `core/instrument.h` and adapters in `instruments/`. This is a versioned, portable C source API,
independent of `hb_module.h`, initially statically linked. A future binary plugin ABI is outside v1.
The reviewed source contracts live in `core/param.h`, `core/event.h`, `core/instrument.h`, `core/effect.h` and
`core/project_format.h`; [decision 002](work/decisions/002-contracts.md) defines semantics and wire offsets.
[Decision 005](work/decisions/005-parameter-readback.md) adds current-parameter readback in source API2;
settings/project wire versions remain1.
Descriptors are immutable; each instance has caller-owned storage with declared size/alignment. Two ARPs share
no mutable state. Core operations (implementation status is tracked in the task ledger):

```c
const md_instrument_desc_t *md_instrument_find(const md_instrument_registry_t *, uint32_t instrument_id);
int  md_instrument_init(md_instrument_t *, const md_instrument_desc_t *,
                        void *state, uint32_t state_bytes, uint32_t sample_rate);
int  md_instrument_event(md_instrument_t *, const md_event_t *);
int  md_instrument_param_get(const md_instrument_t *, md_param_id_t id, int32_t *value);
void md_instrument_render_add(md_instrument_t *, int32_t *stereo, uint32_t frames);
void md_instrument_reset(md_instrument_t *);
int  md_sound_encode(const md_instrument_t *, uint8_t *dst, uint32_t capacity);
int  md_sound_validate(const md_instrument_desc_t *, uint16_t version, const uint8_t *, uint32_t bytes);
int  md_sound_apply(md_instrument_t *, uint16_t version, const uint8_t *, uint32_t bytes);
```

Encoding returns bytes written or an error; validate/apply return status. Validation has no side effects;
apply only validated data at a safe boundary. Reset cuts voices and clears DSP history while retaining the
sound. Normal note release preserves tails. Module teardown releases all ownership and resets instances.

Descriptors contain stable instrument ID, API/sound versions, name, supported sample rates, state size/alignment,
maximum polyphony, presets and parameter metadata. Capabilities distinguish mono/poly, percussion, velocity,
explicit accent, legato/slide, pressure and pitch bend. Unsupported events have defined results and disabled UI.

Each parameter has a stable 16-bit numeric ID, name, native integer min/max/default, unit/scaling/enum labels, and
flags for automation and smoothing. Store IDs, never array positions or pointers. Define adapter conversion
and smoothing per parameter; discrete registration/patch changes are not interpolated. Panel, MIDI, presets
and automation use the same parameter path. Sound serialization saves parameter values, not raw engine structs.
Both instrument/effect descriptors expose a pure `get_param` callback for every exposed parameter; validated
`md_instrument_param_get` / `md_effect_param_get` wrappers provide current values after opaque preset loads.
Read these only in the audio/stopped owner and publish UI snapshots. Read-only values are readable; failures
leave caller output unchanged. The project model retains base values separately while locks override DSP:
readback is current/effective state, not a way to rediscover the base underneath a lock.

Events include note-on/off, drum hit, parameter set, bend, pressure, source release and panic. Note-on includes
pitch, velocity, explicit accent/legato flags and a unique token; note-off references that token. Chord expansion
belongs to the sequencer, not Eminent. Kit voice IDs are stable and mapped by the MiniPops adapter. MIDI channels
are routing configuration, not instrument identity.

The router tracks source (sequencer track, panel, MIDI input/channel) and note token. A sequenced note-off cannot
release a live note at the same pitch. Mono adapters specify last-note priority: a held live note overrides the
sequenced line, and release resumes the surviving sequenced note. Poly adapters reference-count same-pitch
ownership where their engine cannot represent duplicate voices. Mute releases that track's sequenced voices;
panic and project replacement release all. Sustain is source-scoped and cannot retain notes after panic.

`render_add` supports spans 1..256 frames, preserves state and never clears the caller's buffer. No allocation,
host calls, hardware access, locks or waits in instrument rendering/event dispatch. Test repeated short spans:
engines may update coefficients per render call, so block splitting is a correctness and CPU question.

## Audio ownership and event scheduling

`core/session.*` connects transport, sequencer, router, instruments and mixer. Audio is the sole writer of live
engine state, playing position and note ownership. UI/panel/MIDI submit bounded commands and read published
snapshots. Document actual callback contexts before selecting queues: SPSC is valid only after producers are
serialized in one context. `volatile` is not synchronization.

Accept complete edits at defined boundaries through fixed buffers and acknowledgements; a producer cannot
reuse a buffer while audio reads it. No serialization, flash work or full-project copies in the sample loop.
Keep bounded staging and one-level undo for the last recording pass/destructive edit outside the stored project.

Merge sequencer, scheduled releases and timestamped input events in sample order. Render to the next event,
apply its events, then render onward. Offset 0 applies before the first sample; offset n belongs to the next
block. Equal-timestamp order: stop/panic/source release, section/pattern transition, restore parameters/apply
locks, note-offs, note-ons/hits. Mono slide cleanup is the explicit exception: deliver the new slide note
before releasing its predecessor at the same sample; the iterator exposes that final cleanup priority. Use a bounded event iterator rather than an unbounded block-sized event array.

Derive limits from simultaneous chord expansions, 14 drum hits, pending releases, locks and MIDI input. Reserve
stop/release capacity. Reject excess edits/new notes visibly; if a release cannot be retained, request a bounded
emergency source flush. Never silently lose a note-off, spin in render or run unlimited catch-up after a clock
gap. Expose queue high-water marks and overflow counters to tests.

## Sequencer specification

Use portable `core/transport.*` and `module/seq.h` / `module/seq.c`, emitting instrument-neutral events without
ARP/MiniPops structs, host calls or panel IDs. v1 has four tracks, four patterns per track, lengths 1..64 steps,
32 song sections and fixed 4/4. These are capacity decisions, not questions deferred to screen implementation.

### Clock and transport

One clock drives notes, hits, gates, songs, tempo-synced echo and outgoing clock. Use 96 PPQN internally: 1/32,
1/16, 1/8, 1/4, 1/2 and bar divisions are integral, and MIDI clock is one pulse per four internal ticks. Keep
musical position separate from wrapping sample time. Map ticks to samples with fractional remainder accumulation;
constant-tempo error stays below one sample relative to the ideal timeline. Repeatedly truncating Q8 samples
per tick causes drift and is not sufficient.

Tempo 40..240 BPM in v1. Recompute conversions on tempo changes, preserving musical phase and pending musical
gate durations. No 64-bit division per sample; wide setup intermediates are acceptable after target measurement.
Swing delays alternate steps within a pair without changing pair duration: 50% is straight, maximum 75%.
Specify and test alternation across odd-length pattern wraps.

Start resets to the chosen position/seed. Stop releases sequenced notes while retaining live input and tails;
continue resumes retained position. Panic cuts everything. Pattern launches default to next bar, with explicit
immediate mode (next hundredth-tick boundary, <=7 samples at40BPM) and a visible pending launch. Song sections reset track phase except HOLD, which preserves it.

### Patterns, notes, drums and automation

Tracks bind to instrument instances, initially BASS, LEAD, CHORDS and DRUMS. Pattern modes are mono, chord and
drum, checked against capabilities. Metadata includes length, division, swing and transpose. Track settings
include binding, MIDI/input routing, level, pan, sends and mute. ARP 1601 and MiniPops internal playback stay off
inside meloDock; their standalone modules retain them.

Use a six-byte wire step: `payload:u16`, `velocity:u8`, `gate:u8`, `flags:u8`, `probability:u8`. Serialize explicitly
in little-endian order, never by dumping a C struct.

- Mono payload stores full MIDI pitch; chord payload stores full MIDI root plus quality (major/minor, sevenths,
  sus2/sus4, fifth), expanded to at most four notes. Chord root retains its octave.
- Drum payload is a 14-bit simultaneous-hit mask. Hits in one step share velocity in v1. Per-hit velocity and
  arbitrary recorded polyphony need a later schema; do not silently discard unsupported input.
- Gate 0 means rest; 1..200 means percent of the straight step duration. Flags include tie, accent and slide.
  Ties extend the most recent note/chord group without an attack and retain its pitches. Slide is monophonic
  in v1, requires adapter support, and falls back to normal note-on without a surviving predecessor.
  Rests/probability skips must not strand notes; drums ignore pitched gate/slide controls.
- Probability 0..100 is decided once per step visit using project seed, track and loop identity, independent of
  render partition and unrelated live input. Specify repeatability for restart and seek.

Mono onset replaces the previous sequenced note unless tied/slid; overlapping chord gates use tokens and a
bounded pool. Specify adapter voice stealing. Reject pitches outside 0..127 after transpose rather than wrapping.
v1 stores one mono note or recognized chord per step, not arbitrary piano-roll polyphony.

Support 48 sparse locks per project, one per step: step reference, stable parameter ID, signed 16-bit native
value. Only parameters fitting that range are lockable. A lock overrides the base sound until the next step,
including rest/skipped steps, when base is restored before the next lock. Panel edits update the underlying base.
Stop, pattern replacement and sound change restore base. Reject capacity exhaustion visibly; do not destructively
rewrite sound patches to implement automation.

Import ARP sequences and MiniPops rhythm combinations into editable patterns. Preserve simultaneous hits and
measure flam timing loss. Source timings that cannot fit the v1 grid must report quantization; the standalone
engine is the reference. Microtiming/ratchets and finer drum lanes are follow-ups, not unbudgeted v1 promises.

### Songs, recording and performance

A section specifies a pattern/HOLD/REST per track, 1..64 bars, pitched transpose and mute mask. HOLD preserves
phase, REST releases the track and silences its sequencer, normal pattern selection resets phase. Song loops
or stops at its end. Section duration uses global bars, independently of pattern lengths/divisions. Seek
reconstructs phase/automation deterministically without replaying audio. Display section/bar and selected step.

Ship mute/solo, queued pattern/section launch, pattern copy/paste/clear, half/double length within capacity and
one-level undo. Live playing and sound editing remain available while running. Sound changes are track-level
operations in v1; per-pattern sound snapshots need a later storage decision.

REC arms one track. Stopped: step entry advances after note/chord release, with explicit rest/tie actions.
Running: count-in, quantized overdub and replace for one pattern pass, note-off capture, and undo of the pass.
Quantize to nearest onset, ties forward, wrap correctly and derive gate from release. Buffer chords until release
or a defined collection window; document root/quality recognition and visibly reject unsupported chords. Drum
input maps to kit voices and merges hits. Record quantization is separate from swing to avoid applying it twice.

Step-page keys edit; live/sound-page keys play the selected input destination, lead by default. Audition, record
and playback routing are explicit. Key-up releases the token from key-down even after page/octave/track changes.

## Mixer and effects

Render parts into bounded stereo buffers, apply smoothed level/pan and echo sends, accumulate with documented
headroom and saturate at the output boundary. Full strings, lead, bass and drums must never wrap. Show clipping
before host master gain. Additional instruments must pass the same contract, not add a parallel mixer path.

Musical echo and reverb belong in meloDock so independent sends and project recall work on regular dock builds.
The dock retains device EQ/output protection and may add an optional global room effect. Request its room OFF
by default through a capability-checked call outside render; show any forced host override as GLOBAL ROOM.
Never silently apply two musical reverbs. Lack of a host override API is not a reason to lose local effects.

The initial fixed routing graph is:

```text
per-track instrument -> insert 1 -> insert 2 -> level/pan -> dry mix
                                               |-> echo send -> echo return ----|
                                               |-> reverb send -> reverb return-|
dry mix + returns -> master sweep filter (bypass default) -> dock output
```

Sends are post-insert, post-level/pan in v1; mute removes the track's input but lets existing returns decay.
No cross-feedback between shared buses in v1. The master filter sweeps the dry mix and returns together; an
echo's own tone filter belongs inside its feedback loop. No arbitrary graph editor or processor sharing across
tracks. Two insert slots per track is the maximum graph shape, not a guarantee that every expensive combination
meets CPU/RAM/storage limits. Start with bypass, then measure the supported factory combinations.

Add `core/effect.h`: immutable descriptors, caller-owned instance state, stable IDs/versioned parameters, bounded
`process_replace(stereo, frames)` for inserts, reset, tail policy and explicit settings encode/validate/apply.
Bus processors take a separate input and produce wet-only output; gain/mix belongs to the mixer. In-place/alias
rules and parameter smoothing must be explicit. Share parameter conventions with instruments, not note events.
Effects consume tempo snapshots and parameter commands, never run another transport. Panic clears effect history;
normal stop preserves tails. Project replacement resets tails to prevent leaking sound between projects.

Instrument filters remain inside their voices when note envelopes, keyboard tracking or per-note modulation
drive them. Reusable DSP may implement both a voice filter and an insert filter without sharing live state.
Eminent's existing ensemble/phaser remain integrated initially to preserve its reference sound. A later extraction
must preserve output and migrate the sound without activating both integrated and insert copies by default.

Ship a resonant low-pass/band-pass insert, a slow phaser, tape echo, local reverb and the bypassable master sweep.
Start with cascaded low-pass or high-pass/low-pass shaping; parallel formant banks are later extensions. Expose
master cutoff/resonance with a performance control and allow track insert parameters through automation targets.
The lock's 16-bit parameter ID is session-scoped: allocate disjoint ranges to instrument, inserts, mixer and
global buses/master. Global targets have a single owning automation lane per project; reject competing locks
instead of making track iteration order audible. The exact range map is part of the contract task.

## Electronic FX is an instrument

Add a small `instruments/electronic_fx.*` instrument with DROP, PING, BUBBLE, CHIRP and METAL presets: pitch and
amplitude envelopes, resonant-filter excitation, and optional FM/ring-modulated partials. Trigger with ordinary
note/hit events; expose pitch sweep, decay, tone/resonance and modulation depth through descriptors and locks.
Deterministic randomness comes from instance seed. Delay/reverb are sends, not embedded copies per FX voice.

This creates sound, unlike the bus sweep which processes sound. Bind it to any existing track in mono or drum
mode according to declared capabilities; it does not add a fifth track. Drum-mode kit IDs select FX recipes,
mono mode plays the current recipe melodically. Document pitch/gate behavior for both. Test maximum simultaneous
hits, bounded voice stealing, decaying-to-silence behavior and render partition independence.

## Sound library and editor

**Names.** Factory sounds, sequences and rhythms carry descriptive names (WARM BASS, PLUCK SEQ, ROOT PULSE), never
an artist's name or a record or track title. Comments may say which style a sound follows; nothing a player sees
names a record. The ARP 2600's ten patches and eight sequences were renamed this way on 2026-11-07, in fm1-harbor
and taken here.

**Twenty sounds a part.** Each ARP part (lead, bass) chooses from at least twenty sounds: SOFT LEAD, the ten
factory patches and nine new ones. Candidates are a breathy flute, a hollow square, a slow-sweep pad lead, a
glassy sine, a resonant whistle, a sync lead, a soft brass, a vibrato lead and a noise-breath voice. Factory
sounds are flash constants; flash has room. PRESETS and ALGORITHM step through the list, MIDI program change
selects by position, and the screen shows position and name. The same list mechanism serves the strings'
registrations and the effects instrument's recipes.

**Editor.** A sound page edits the selected part's current sound through its descriptor: named rows (OSC, FILTER,
ENVELOPES, LFO and MOD, AMP), SEL for the next row, KNOB1..4 the row's four parameters, values and units on
screen. Edits go through session parameter commands, never directly to the DSP, and work while playing. One-level
undo restores the sound before editing began. Sound locks still override the edited base during playback.

**Where an edited sound lives.** The track's sound is already part of the project (the sound pool above: an ARP
sound is 36 bytes), so an edited sound saves with its project. A library of user sounds shared between projects
needs storage the module does not yet have: the settings record is 240 bytes and the project bank is full. Decide
before building it: (a) user sounds only inside projects, with copy between parts; (b) a few user slots in the
project's sound pool, checked by admission; or (c) a host storage extension for a separate sound bank. Until
then, offer (a).

## Projects and storage

Freeze a versioned schema with magic, length, checksum, stable instrument IDs, sound versions and explicit
little-endian fields. Unknown instrument/schema versions reject loading and preserve the current project.
Validate counts, ranges, references and lengths before publication. Never persist pointers, queues or DSP state.

| Wire region | Reserved bytes |
| --- | ---: |
| Header, name, tempo, seed, mixer/FX globals | 128 |
| Four track settings | 128 |
| Sixteen pattern metadata records | 128 |
| 4 tracks × 4 patterns × 64 steps × 6 bytes | 6144 |
| 32 sections × 16 bytes | 512 |
| 48 locks × 6 bytes | 288 |
| Versioned instrument sounds/directory | 512 |
| Insert, return and master-filter settings/directory | 256 |
| **Total / primary bank limit** | **8096 / 8176** |

The margin is only 80 bytes. This revises the earlier 64-lock/640-byte-sound proposal to budget project effects.
Prove field layouts and maximum serialized size before freezing v1. Sounds and effects occupy
a bounded pool; an instrument needing larger sound storage must fail admission visibly or require a revised
schema. Reject chain/sound edits that exceed the pool before accepting the edit, not at SAVE. Runtime state,
effects delay lines, command queues, undo, editing and save staging add RAM beyond this wire budget.

SAVE while running marks a pending save and shows STOP TO SAVE; never unexpectedly stop a take. Once stopped,
obtain a coherent snapshot and safely suspend audio around erase according to the host contract. Show progress,
report errors and retain dirty state until success. Loading is stopped, validated in staging, then published as
a complete project. Boot never autoplays.

Erase-in-place bank saves do not promise the previous project survives power loss. Checksum detects damage;
it does not make a write atomic. Test corrupt-bank fallback to a factory project. Last-good recovery requires
two reserved banks with generation/commit records or an atomic host service, plus interrupted-write tests.
Optional indexed-bank slots require `HB_HOST_HAS`, free-capacity checks and allocation failure UI. One project
must work on old hosts without indexed banks.

Keep UI preferences in the <=240-byte settings record. Migrate today's small ORRERY settings explicitly
where possible. Add computer export/import over `HB_F_DATA`, validating chunk order, duplicate chunks, bounds
and checksum. This provides backup independent of extra flash capacity; it is not Melodee file compatibility.

## MIDI and required host work

Map channels to parts; program/CC/velocity/bend/pressure use capabilities and the shared event path. Support
note-on velocity zero as off, source-scoped sustain, CC120/123 and disconnect cleanup. No MIDI bypasses ownership.

Deliver internal-clock operation first. A separate fm1-harbor proposal specifies source-tagged timestamped
real-time/system-common delivery for supported USB/DIN/BLE paths: F8, FA, FB, FC and F2 Song Position (one unit
is six clocks, a sixteenth). Preserve running status around real-time bytes. Define timestamp-to-sample mapping,
wrap, lateness, clock-source choice and timeout. Untimestamped delivery has block latency and cannot be called
sample-accurate external synchronization.

Render queues outgoing MIDI for host/main-loop transmission; never calls `midi_send` itself. Today's API lacks
scheduled deadlines, so measure jitter first. Accurate clock out needs a host deadline queue, transport priority,
explicit overflow policy and byte-level tests for one-byte real-time and three-byte F2. Do not assume the
channel-message sender already handles those. Receive uses bounded phase correction, not repeated hard resets.
Missing clock stops/releases sequenced notes visibly. One master source/direction prevents clock/thru loops.

## Panel workflow

Use the 14 buttons, seven encoders and 27 keys. Primary pages take one short press; advanced controls use a
visible named row/subpage. Prototype this mapping in the emulator before freezing it:

| Control | Proposed behavior |
| --- | --- |
| EDIT / GLO / ENV / LFO | Strings / lead / bass / drums sounds, descriptor-based parameters with curated knob groups |
| SEQ | STEPS; repeated press cycles track; first 16 keys edit steps, OCT-/OCT+ change 16-step page |
| ARP | SONG: SELECT section, KNOB1..4 pattern/HOLD/REST, ALGORITHM bars, PRESETS transpose |
| SEL | Short: LIVE from sound/FX pages, next named row on edit pages; hold 0.6 s: LIVE from any page |
| FX | Cycle MIX / INSERTS / RETURNS / MASTER; track processors and shared buses show their actual scope |
| PLAY | Start/stop; ARP held + PLAY starts from selected section; visible transport action selects continue |
| REC | Arm/disarm selected track; named record row selects step/overdub/replace and count-in |
| SAVE | Save stopped, otherwise pending; show dirty/saved/error status |
| HOME | Host long hold opens picker; HOME+PLAY provides panic before that threshold |

STEPS default row: SELECT cursor, KNOB1 pitch/root/drum voice, KNOB2 gate, KNOB3 velocity, KNOB4 probability,
ALGORITHM division, PRESETS pattern. SEL cycles named rows for chord quality/drum mask, tie/slide/accent, locks,
pattern settings (length/swing/transpose), copy/paste/clear/undo, and recording mode/count-in. SELECT retains
the step cursor across rows; operation rows use a displayed selection and REC to execute instead of arming.
SONG's alternate rows expose four mute toggles on KNOB1..4 and the loop/end setting. LIVE's knobs are the four
levels, SELECT selects track and ALGORITHM edits tempo. Keys normally play the live destination; holding SEL
makes keys 1..4 mute, 5..8 solo, and 9..24 queue the four patterns of each of four tracks. Show that modifier
map on screen. Suppress the short-press action when a modifier chord or hold was consumed. Document key-up
handling, and ensure HOME modifiers cannot accidentally leave the module.

On INSERTS, SELECT chooses track, ALGORITHM slot, PRESETS effect/bypass, and KNOB1..4 edit the displayed
parameter row (SEL advances rows). RETURNS selects echo/reverb and exposes their parameters plus four track
sends on named rows. MASTER exposes filter type/bypass, cutoff, resonance and its supported tone controls.
FX held + KNOB1/KNOB2 gives direct master cutoff/resonance from any page; consume the modifier so release
does not change pages. Treat the graph and settings pool limits as admission checks when selecting a processor.

Header shows tempo/clock source, transport/record state, section/bar/step, selected track/pattern, pending launch
and dirty state. Distinguish playhead, active steps and audition with LEDs. Acceptance scenarios: enter a bassline,
create chord/drum patterns, copy them, record a lead, arrange sections, undo a pass, save and reload. A feature
without a workable panel path is not finished.

## Why not port all of Melodee?

The old reason, “Melodee is separate firmware”, is insufficient: extracting its sequencer or integrating our
engines there could still be cheaper. [Melodee's upstream README](https://github.com/keremimo/melodee#readme),
reviewed 2026-11-05, describes four internal parts plus four MIDI tracks, 64-section songs, 32 projects and a
shared effects rack; current builds use both cores. It is an existing workstation. This plan does not promise
feature parity or assume a rewrite is easier.

The reason to avoid a **whole-firmware port** is ownership: this project needs composable instruments under
the dock's hardware/audio/MIDI/storage services. Carrying another firmware's platform assumptions across that
boundary creates two places responsible for those services, while engine adaptation and timing integration
still need doing. This is a cost hypothesis to verify in source, not proof of impossibility or a GPL objection.

Before implementing the sequencer, time-box a source audit to one or two engineering days. Record the exact
upstream commit and compare:

| Path | Evidence needed |
| --- | --- |
| Extract/adapt Melodee sequencer | Can it run in a native harness with injected clock/storage and instrument event sink? What UI/platform/global-state coupling remains? |
| Put our engines into Melodee | What adapter work is needed, and is its complete workflow preferable enough to make dock composability unnecessary? |
| New meloDock core | Can this API and bounded four-track vertical slice remain smaller/testable while delivering the required editing/recording workflow? |

Inspect transport, patterns, recording, song, mixer, persistence and relevant tests. Count retained versus replaced
subsystems and demonstrate an extraction seam. Review actual licence files and preserve attribution for reused
code. Reuse separable sequencer code, algorithms or tests when cheaper; do not import its hardware/update/storage
platform into a module.

Proceed with a new core only if that audit supports it. Its defensible benefit is the reusable instrument boundary
and compact dock-native composition workflow with explicit smaller v1 scope. If adapting Melodee's tested core
meets that boundary with less work, choose it. If the goal becomes full Melodee functionality, integrate these
engines there rather than maintain a second indefinitely expanding workstation.

## Delivery order and acceptance gates

1. **Baseline and reuse decision.** Record engine/host revisions, reference renders, RAM/code/block cost and
   current behavior. Finish the Melodee audit and document the decision. No sequencer/UI rewrite before choosing
   the core and agreeing the instrument boundary.
2. **Instrument API vertical slice.** Registry, descriptors, caller-owned instances, routing, ownership, sound
   encoding and fake instrument. Adapt one ARP, then a second independent ARP, Eminent and MiniPops. Native tests:
   live/sequence duplicate pitches, mono priority, panic, parameter ranges, sound round trips and unsupported
   capabilities. Upstream explicit ARP accent/slide support and verify standalone regression checks.
3. **Clock and event renderer.** Native C harness driven by `tests/seq_check.py`: long-run timing, counter wrap,
   tempo/swing, start/stop/continue and release ordering. Identical event traces over blocks 1/17/64/255/256 and
   irregular partitions. Test event limits/overflow and adapter behavior under short render spans.
4. **Playable patterns.** All four tracks drive only the API: drum hits, chords, ties, chance, locks, mixer,
   launch/mute/solo, local echo/reverb, insert/master filters, electronic FX, sound pages, LIVE/STEPS,
   copy/clear/undo. Port the demo and compare actual onsets at sample
   boundaries, rather than polling positions from the main loop.
5. **Recording and songs.** SONG, count-in, step entry, overdub/replace, release capture and pass undo. Test
   independent pattern lengths, HOLD/REST, section/loop boundaries, seek, live override and page/octave changes
   while holding keys. Complete the panel scenarios before adding scope.
6. **Projects.** Freeze the proven <=8176-byte schema, validation, migration, stopped save/load and error UI.
   Maximum-size round trip, bad CRC, truncation/unknown data, full banks, corrupt-save boot fallback and export/
   import. Claim last-good recovery only after atomic storage and interrupted-write tests exist.
7. **External synchronization.** Separate host PR for timestamped input and scheduled output. Test interleaved
   real-time bytes, F2/Continue, clock drift/jitter, timeout, feedback prevention and source switching. Validate
   each transport independently; emulator results are not hardware proof.
8. **Release and content.** Demo plus two complete factory songs, actual panel/API/schema documentation and
   measured limits. Regular dock and seqdock emulator paths. Hardware deployment requires owner permission;
   never send a build to a real FM-1 automatically.

At every audio milestone report code, data/BSS, RAM code, stack/staging/queue use and worst block cost. Target
<=70% worst block in a defined stress case: maximum strings, held lead, bass, dense drums, echo, locks/recording
and the supported local effects graph. Measure any forced host room separately. Require no underruns over at
least ten minutes, no clipping wrap and no stuck notes.
Measure cold start and short-span rendering. Missed budget triggers optimization/scope reduction before more
tracks/effects; historical spare cycles are not a guarantee.

Use reference renders/spectrograms for musical regressions and native tests for event correctness. Compare
segmented and full-block output with a documented numerical tolerance where block-rate DSP differs. Deferred
from v1: arbitrary polyphonic piano-roll recording, microtiming/ratchets, per-pattern sounds, extra tracks,
dynamic `.dock` composition and full Melodee compatibility. Revisit after the four-part workflow passes the gates.
