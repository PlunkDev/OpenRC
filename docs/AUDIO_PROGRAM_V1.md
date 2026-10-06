# Neutral audio control graph V1

`AudioProgramBankV1` owns an integer control clock, a shared parameterized XOR
word ring, and bounded typed graphs. Nodes describe delays, voice requests,
release requests, scalar assignment/addition/comparison, random branches and
prepared table oscillators. Voice indices are neutral binding indices; sample
decoders, original grain records, addresses and register layouts are absent.

A node waits its own pre-delay before executing. A random wait adds to its
successor's pre-delay. Zero-delay successors execute in the same tick, subject
to an explicit action bound. Programs execute in admission order; oscillators
then update on divisible global ticks. Admission preserves the global clock
and shared random owner. Oscillator activation samples before advancing phase;
later updates retain fractional phase. A random branch may retain its previous
choice per instance or in a shared bank owner. Source structured random blocks
are lowered to ordinary graph successors rather than a runtime grain counter.

`advance_idle_to(absolute_tick)` advances only an unstopped owner with no live
instances and no pending events, and rejects a backward target. Its sole
mutation is the absolute clock. This preserves initialized random state and
the eventual admission/modulation parity without iterating empty control ticks.

Voice emission immediately increments an instance's logical `owned_count`,
including zero-delay admission work. `AudioProgramWaitOwnedV1` retries its same
node after one tick while that count is nonzero. With zero ownership it advances
to the successor in the current tick. `AudioProgramReleaseV1` emits a release
request and immediately detaches all logical ownership; physical release tails
remain the host's responsibility and no longer block the program.

`complete_owned(instance,count)` accepts only positive counts no larger than
the remaining logical ownership. The host must match each callback to its
still-attached voice identity before invoking this API: an old detached tail
must never decrement a subsequently admitted voice in the same instance.
Completion changes ownership without advancing the graph or clock. An ended
graph with zero logical ownership stops its oscillators immediately, before any
subsequent global modulation step.

At most one `AudioProgramStopSectionV1` node is allowed per graph. Normal flow
ends there and skips its successor. `stop_instance(instance)` instead enters
that successor once, keeping explicitly encoded delays and actions. It does
not infer release or completion. A source compiler supplies explicit release
actions, including any release required before its source stop tail. Repeated
stop calls do not replay the tail or restart its delay. A tail that admits new
voices retains modulation until those voices complete or detach.

`retire(instance)` requires an ended graph and zero logical ownership. It frees
instance capacity while preserving the admission order of remaining owners.
New instances receive monotonically increasing tokens; stale callbacks cannot
target a later instance by token reuse. Resource/device lifetime and detached
tails remain separate host owners.

Events carry the exact tick, instance token, program key, voice binding and a
snapshot of eight integer scalars. The current instance scalars remain readable
for subsequently changing controls. Graph completion does not mean its voices
have completed. `stop()` stops graph/modulation work without inventing an audio
completion acknowledgement. Device and voice retirement remain separate owners.
Failed bounded admission/tick/stop-entry operations restore random, scalar,
branch, logical ownership, stop latch and event state. Taking events reserves the replacement queue before transferring
ownership, so an allocation failure cannot discard queued requests.

The `ORAUPRG1` schema-1 resource has a 64-byte envelope and SHA-256 body digest.
The body stores explicit clocks, the ring state, graph owners and typed action
payloads. Readers verify counts, each payload boundary, reserved flags, exact
byte consumption and graph references. Default limits are 16 MiB, 64 graphs,
4096 nodes per graph, 64 live instances, 256 pending events and 4096 actions per
tick. Appended zero-payload action kinds 10 and 11 encode wait-owned and
stop-section respectively, retaining the existing header and earlier action
encodings. Unknown kinds and nonempty payloads for those actions are rejected.

## Original frontend control qualification

`compile_rac_frontend_sound_programs_v1` lowers the five reached frontend
descriptors 2, 3, 8, 4 and 9 into five graphs and 26 tone bindings. It produces
a 28,428-byte control resource. Thirteen decoded sample owners remain separate
`AudioStreamV1` resources. The companion voice bank supplies qualified gain,
pitch, envelope and sample-end data; a control resource alone does not contain
playable sample ownership.

The ignored CMake source probe now admits valid original type-5 handler tokens
at their actual owned pool layout. Its earlier diagnostic used descriptor IDs
as tokens, which caused the original lookup to reject bend writes silently.
The corrected differential requires nonzero bend writes and compares 12,000
ticks: 36,003 complete scalar states, 104 events, and all 250 random words after
every step. All match after neutral encoding/decoding, across 2,472,819 executed
original instructions. There are 102 tone requests, two release requests and
68 random draws in this explicitly bounded three-program fixture.

Source timing, control and modulation are proved here with successful voice
allocation and device calls recorded as external requests. This does not prove
voice completion, actual device mixing, later scenic programs or live admission
timing. The separately qualified startup clock must not be reset when these
graphs are admitted. Logs and generated resource remain ignored under
`local/forensics/audio-program-*.log` and
`local/forensics/frontend-transition/iop-modules/ambient-program.orauprog`.

The complete menu can remain active beyond the initial three admissions. Later
scenic cues reach descriptor 4 (61 grains, 11 tone bindings) and descriptor 9
(one tone). These add eight sample owners, bringing ambient sample ownership
to thirteen; the five existing menu samples complete the original eighteen.
After its ownership wait, descriptor 4 branches back to its first label. Its
ordinary graph repeats; it does not naturally reach its stop marker. Descriptor
9 ends its graph immediately and retires only after its voice completes.

The expanded original-instruction differential executes all five programs after
neutral encode/decode: 12,000 natural ticks and a separate stop after three
blocked wait observations at tick 320. It compares 49,681 complete scalar,
graph and ownership states, 357 events, 334 original completion callbacks and
all 250 random words after every step. All match across 8,843,713 original
instructions. The long case reaches every one of descriptor 4's seven random
branches, 118 blocked wait observations and 76 cleared waits/restarts. The
explicit-stop case executes the prepared pre-tail release and original tail.
Normal stop-marker skipping has a separate executed source fixture because
that marker is not naturally reached by descriptor 4's repeating graph.

This differential runs the original physical list, masks, callback and scheduler
instructions with an explicit successful-allocation boundary and scripted ENVX
observations. It proves control/lifecycle correspondence under those inputs,
not the duration of physical playback. The log is
`local/forensics/audio-full-program-source.log`; generated source evidence stays
in the ignored frontend-transition diagnostic directory.

The neutral lifecycle tests exercise same-tick voice/wait and release/wait
ordering, externally observed completion, detached tails, skipped versus
explicit stop paths, retained tail modulation, transactional failed stops,
ordered slot retirement and rejection of stale tokens. Idle advancement tests
also preserve all random state and retained modulation parity while rejecting
active instances, pending events and backward movement. The current lifecycle
and idle tests passed the CMake build, PE audit and execution.
