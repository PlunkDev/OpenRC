# Neutral voice bank V1

`AudioVoiceBankV1` binds a prepared program's `(program_key, voice_index)` to
explicit stream, gain-table and phase-curve owners. Program, streams and gain
tables are separate neutral resources named by exact resource IDs. The bank
contains the deduplicated phase curves, numeric envelope stages and optional
finite-input read-ahead policy. Source tone bytes and hardware registers do not
cross this boundary.

Level, pan and phase controls use bounded scalar projections. Each term selects
either the voice's admission snapshot or the current program instance, multiplies
and divides with truncation toward zero, and contributes to an integer sum.
The prepared projection then clamps or wraps that sum in its explicit domain.
Snapshot terms retain their original values when program locals subsequently
change. Gain tables retain phase continuity independently for each voice.

`AudioProgramCuesV1` is a separate resource linking the voice bank to its exact
scene timeline. Each row maintains a logical program owner inside an inclusive
sample-index window. Missing or completed owners can be admitted again while
the window remains active; a valid owner outside the window is stopped. The
caller supplies qualified handle validity at the scene observation boundary.
The `ORAUCUE1` schema-1 codec authenticates all rows and resource IDs and checks
their bounds and unique cue keys. The cue clock is the scene sample index,
independent of the continuously running PCM/program observation clock.

The observation policy supplies a PCM-frame stride, an offset from the host's
one persistent clock origin, and explicit zero-observation counts for logical
completion and physical retirement. Its order is observation, program work,
release commit, start commit, then modulation. A pending start cannot render or
be observed until the start stage commits it. Admission does not reset the
observation grid, the shared random state or the program clock.

`AudioVoicePlayerV1` composes a stream with its prepared envelope and optional
read-ahead policy. It checks input availability before advancing the envelope;
the frame uses the newly reached level. A release retains the current level.
The final zero-envelope frame is rendered and advances phase before stopping.
An input-end event occurs before that frame's output. These local stop reasons
do not imply logical program completion or device-buffer retirement.

The `ORAVBNK1` schema-1 envelope is 64 bytes and authenticates its complete body
with SHA-256. Readers enforce bounded counts, exact byte ownership, unique
resource handles and binding keys, curve domains, projection arithmetic and
envelope/read-ahead validity. Runtime admission must additionally resolve every
handle to the expected resource type and check its table dimensions, gain and
phase domains and program voice indices. Default limits are 16 MiB, 64 stream
and gain handles, 64 curves of at most 65,536 values, and 1,024 bindings.

## Compiler qualification

The frontend compiler lowers five scenic programs to 26 voice bindings and
thirteen distinct input streams, including two exact predictive-history loops.
Four gain-table families retain six phase classes each. Pitch, gain and envelope
conversion remain compiler operations; the runtime consumes numeric tables and
stages. The default invocation profile is effects volume 1024 and stereo; a
different profile requires separately prepared controls.

Original instruction execution qualifies pitch and signed gain conversion.
The expanded gain differential corrected the quadratic divisor to 32766;
the five existing menu PCM payloads remained byte-identical. The original
control/RNG and retirement fixtures remain separate from PCM reference tests.
Original physical-start routing also qualifies zero wet-send flags for all
31 frontend tones; the ambient compiler rejects other flags. This local dry
routing proof does not establish historical effect-tail or disc-stream state.

The envelope and decode-window timing use the published software reference in
[PCSX2 ADSR.cpp](https://raw.githubusercontent.com/PCSX2/pcsx2/master/pcsx2/SPU2/ADSR.cpp)
and [Mixer.cpp](https://raw.githubusercontent.com/PCSX2/pcsx2/master/pcsx2/SPU2/Mixer.cpp).
They are not a captured-console waveform comparison. Ignored probes retain
raw-field oracles outside the production runtime. Device, allocation and live
admission timing need their own qualification; resource codec success alone
does not establish playable bank ownership.

The bank playback host admits only acyclic, immediate stop tails that fit its
explicit action and event limits. Delays, positive random waits, owned-voice
waits and new voice admissions in a stop tail are rejected. This bounded
contract lets `stop_all` execute every prepared stop tail before the hard mute;
the generic program scheduler remains available for other authored graphs.
