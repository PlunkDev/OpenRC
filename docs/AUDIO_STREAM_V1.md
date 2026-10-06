# Neutral audio stream v1

`AudioStreamV1` contains decoded mono PCM16, a supplied 256-row four-tap signed
filter, explicit phase/coefficient/gain denominators and an output sample rate.
An optional repeat interval is a suffix of the PCM array. Earlier samples play
once; all later reads repeat that suffix. There is no compressed audio, source
bank structure, executable address, register interpretation or built-in filter
table in the runtime owner.

The source compiler is responsible for proving any decoder-history recurrence
before emitting a repeat. Repeating the bytes of a compressed source block is
not sufficient. `compile_rac_frontend_sound_stream_v1` supplies this resource
from the separately qualified source PCM compiler. Its reached source profile
uses 48000 output frames/s, phase denominator 4096, 256×4 coefficients, and 32768
coefficient/gain denominators. The neutral format itself permits other bounded
rates and denominators with the same explicitly defined arithmetic.

## Rendering and controls

`AudioStreamPlayerV1` owns one admitted resource and a persistent input cursor,
fractional phase, rendered-frame count and finite-exhaustion flag. Each output
frame uses this exact order:

1. Read four input samples at cursor+0/1/2/3. Wrap each lookahead tap separately
   through the repeat suffix; wrapping never resets fractional phase.
2. Select filter row `floor(phase*256/phase_denominator)`.
3. Floor each signed sample/coefficient product divided by the coefficient
   denominator, then add the four results.
4. Floor the envelope multiply divided by the gain denominator.
5. Floor each channel gain multiply divided by that denominator, then clamp
   each result to signed PCM16.
6. Add the frame's phase increment. Advance the integer input cursor by the
   quotient and retain the remainder as phase.

Signed floor is toward negative infinity. Moving a floor after the tap sum or
truncating negative intermediate values toward zero changes output samples.
The magnitude of a four-tap sum is at most 2^32. Envelope and channel-gain
magnitudes are at most 2^30 and cannot amplify after division by their denominator, so
every multiplication is at most 2^62 in magnitude, within signed 64-bit range.
No additional intermediate saturation, wrapping or device-specific arithmetic
is implied. Saturating the tap sum before attenuation changes valid output.

`render` receives caller-owned interleaved stereo output and either one control
record for the whole call or one record per requested frame. Controls contain
phase increment, envelope and signed left/right channel gains. The envelope
is in `[0,gain_denominator]`; channel gains are in
`[-gain_denominator,+gain_denominator]`, preserving channel polarity. Inversion
occurs after the envelope floor. Zero increment is a
valid stationary input position. Every control and output bound is validated
before state or output changes, including the last per-frame record. Rendering
allocates no memory. Arbitrary chunk boundaries preserve samples and state
when the same frame controls are supplied. A stationary finite stream remains
active while its four lookahead samples are available; its rendered-frame
clock and controls still advance. A later nonzero increment resumes the input
cursor with the same fractional phase. Muting changes output values without
pausing phase or finite exhaustion.

Envelope generation, control clocks, pitch modulation, keyoff, voice mixing
and device lifetime are separate owners. The evaluator does not infer those
operations from elapsed time, resource length or source-format metadata.

## Finite input

With both repeat indices zero, input is finite. Rendering stops once four
lookahead samples are unavailable and reports the number of frames written.
Unused output is unchanged. The source menu one-shots have an earlier
prefetch/mute boundary; callers comparing them supply the independently
qualified frame count. Generic input exhaustion does not claim that original
device policy. Existing prepared finite `AudioClipV1` resources are unchanged.

## Encoding and bounds

The little-endian resource uses magic `ORAUSTR1`,
schema 1, a 64-byte envelope and a SHA-256 digest of the body. The body begins
with 48 bytes of timing/count/repeat metadata, followed by 2048 bytes of signed
filter coefficients and then the mono PCM16 samples. Total size is
`2160 + 2*input_sample_count` bytes. Reserved fields must be zero. A repeating
resource requires `repeat_begin < repeat_end == input_sample_count`; finite
input requires at least four samples.

Defaults bound the payload to 64 MiB, input to 32 Mi samples and rendering to
1,048,576 stereo frames per call. Positive phase denominators are bounded by
2^24 and positive coefficient/gain denominators by 2^30. Controls cannot exceed
the admitted phase-increment limit; envelope and channel-gain magnitudes cannot
exceed the gain denominator. Decode checks counts,
byte partitioning, intervals and digest before allocating sample storage.

The neutral test corpus exercises variable increments and gains over 8193
frames, arbitrary chunks, prefix-to-repeat and one-sample repeat crossings,
negative rounding, extreme valid intermediates, finite exhaustion, malformed
codecs and atomic rejection of invalid late controls. It also covers phase
denominators 1, 3, 257 and 2^24 with admitted full-width uint32 increments,
finite hold/resume, attenuation before final clipping, and recomputed valid
digests over invalid payload partitions and uint64 counts. Actual source
comparison is performed separately by the frontend sound probe; this format introduces
no frontend/session schema or native publication-profile changes.

## Verified source comparisons

The audited CMake targets `openrc-audio-stream-tests`,
`openrc-rac-frontend-sound-tests` and
`openrc-rac-frontend-environment-source-probe` pass. The extended ignored probe
compares neutral streaming against the existing independent frame/FIFO/envelope
oracle for all five menu variants at both original stereo settings and the
half-volume mono control: all ten complete output vectors match. Each caller
uses the oracle's actual early-stop frame count. The source-derived leading
silence covers the initial envelope attack; its sustained-envelope reduction
is not generalized to ambient voices.

The two recurring source inputs also match their independently decoded
prefix/repeat fixtures. Descriptor 3 has 26096 prefix samples and 26068 repeat
samples; descriptor 8 has 92372 and 92316 respectively. Their complete neutral
resources occupy 106488 and 371536 bytes. Each stream passes 1,000,000 output
frames against an independent absolute-phase/filter reference, including tap
reads across repeat boundaries and arbitrary render chunks. These loop tests
deliberately supply synthetic changing pitch, envelope and gains; they prove
the stream arithmetic and recurrence, not the authored ambient control graph.

Build/source outputs remain ignored in `local/forensics/build-audio-stream-v1.log`,
`audio-stream-finite-source.log` and `audio-stream-loop-source.log`. That source
validation batch did not relink the CLI or modify the native v12 publication.

The 2026-09-26 boundary review reran the expanded seven neutral test groups:
all pass in the CMake-built, PE-audited `build-portable/openrc-audio-stream-tests.exe`
(SHA-256 `2681c478b0a293e243b904574350cf93eb6e316152134ed71f50239f73d61c10`).
The execution log is `local/forensics/audio-stream-reviewed-tests.log`.
This review required no change to the resource schema, evaluator arithmetic,
or source parity contract.

The subsequent signed-channel extension keeps the resource codec unchanged.
Its eighth test group checks negative fractional gains, inversion after the
envelope floor, both maximum signed polarities, and atomic rejection of
out-of-range gains including both int32 extrema. Variable-control chunk tests
now cross zero in both channels. This permits the independently qualified
ambient phase inversion without adding source register semantics to the player.
All eight groups pass in the rebuilt and explicitly PE-audited portable test
executable (SHA-256
`5c2d5f649c9cd01488120d524c4162d9b4bc50b4703ab959e52fcf886fbd0ed2`);
the log is `local/forensics/audio-stream-signed-tests.log`.
