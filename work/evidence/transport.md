# M2-01 — One exact musical transport

Owner: agent-a. Date: 2026-11-05. Status: submitted for cross-review.

Implemented core/transport.h/.c: independent wall-clock samples and musical position,96PPQN, BPM Q8 within
40..240, start/stop/continue, stopped seek, phase-preserving tempo changes, bounded sample-offset queries and
straight/swing duration helpers. No host, engine, allocation, global mutable state or second clock is involved.

Musical position stores a wrapping quarter count plus0..9599 hundredth-tick units and an exact fractional-unit
remainder.96PPQN remains the tick grid;100 subunits/tick make every supported step's integer gate and swing
percentages representable exactly. MIDI clock would be400units/pulse, but MIDI transmission/reception is not
implemented here. The denominator is44100*60*256=677376000, independent of tempo, so changing BPM Q8 preserves
remainder/phase without rescaling or accumulating truncation. This follows the remainder-carry approach audited
in decision001; no complete upstream function/platform was copied into this source.

Advance splits at most256 frames into chunks <=6. Maximum numerator is6*61440*9600+(677376000-1)=4216319999,
which fits uint32; a widened compile-time assertion protects that bound. Quotient/remainder use32-bit arithmetic.
Event-offset queries compare widened products and binary-search0..limit in at most9 iterations; they use no64-bit
division. The first sample satisfying the rational target is selected, equivalent to ceiling the ideal position,
so constant-tempo event error is below one sample. Offset equal to block length belongs to the next block.

Wall sample time advances while stopped, while musical phase freezes. Stop does not release voices itself:
session/router is responsible for releasing sequence sources while preserving live input and effect tails.
Continue retains phase. Start and stopped seek clear fractional phase at the chosen position but retain wall
sample count. Musical quarter/sample counters wrap independently; comparison horizon is less than2^31quarters
(the exactly ambiguous half-range is rejected). This is vastly beyond the bounded song horizon, not unbounded time.

Swing50 is straight, up to75; even visits last2*base*swing/100 and odd visits last2*base*(100-swing)/100.
The pair length stays2*base. The sequencer must carry visit parity across odd-length pattern wraps, not reset it
with a local step cursor. This transport does not yet store/play patterns, calculate note ownership or record.

Validation:

```sh
python3 tests/run_native.py transport --sanitize on --build-dir build/native-transport
```

PASS strictC11 ASan/UBSan without recovery. Five scenarios:

- Ten minutes plus137frames at eight tempos (40,73,97,120,137,173,240 and73.5BPM), each with partitions
  1/17/64/255/256 and irregular1..256: exact final quarter/unit/remainder equals an independent64-bit oracle.
- Tempo changes with a nonzero remainder, stopped wall-time advancement, continuation, seek and start semantics.
- Thousands of near-future offset queries against a ceiling-division oracle, inclusive block-boundary deferral,
  independent sample and musical-quarter wrap, large position additions and late targets.
- All six divisions, swing50/51/67/75, five fixed plus irregular partitions: event timestamps match independently
  calculated rational times. A96PPQN tick stream uses the same sample-offset contract; odd-loop parity is explicit.
- Invalid tempo/position/frame count rejection leaves state unchanged; invalid division/swing/parity returns0.

`cc -std=c11 -O2 -I . -S -emit-llvm core/transport.c -o build/native-transport/transport.ll` followed by a scan
finds no `udiv i64`/`sdiv i64` operations in native optimized IR. That is supporting inspection, not certification
of target instructions or CPU budget. Firmware integration, physical clock jitter, synchronization, song seeking
and note-off musical duration integration remain subsequent tasks.
