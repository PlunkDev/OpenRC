# Windows prepared audio program owner

`WindowsAudioProgramBankV1` joins the neutral `AudioVoiceBankPlayerV1` to the
existing bounded `WindowsPcmWorkerV1`. Its input owns prepared programs, voices,
streams and gain tables. It contains no RAC/PS2 parsing or source addresses.
Every resource is validated before worker startup. The Windows backend requires
48000 Hz output and uses the bank's prepared observation stride as block size.

The neutral player observes physical voices on its persistent frame grid,
advances the program scheduler, commits releases and starts, then applies live
controls. Event snapshots preserve initial controls before modulation. Pending
start cancellation, four-zero natural completion, detached release tails and
physical retirement remain separate. The gain table is stepped when projected
coordinates change. Logical program retirement does not remove physical tails.

The player's stop contract accepts only bounded, acyclic, immediate stop paths:
no delayed node, voice start, owned-voice wait or nonzero random wait can occur
on a reachable stop path. All branches are checked at admission. `stop_all`
executes those paths in order and hard-stops and retires physical owners. This
restriction avoids claiming that deferred tail work executed during a cancel.

## Commands and acknowledgement

The caller owns one control thread. Only the PCM worker advances programs and
the mixer. `queue(key, frame)` returns an external owner token after reserving
the command and both eventual event slots. It does not claim the worker has
already admitted the program or that audio has played. A full or late queue
does not reserve an owner or silently change the requested frame.

The worker emits `admitted`, followed by exactly one logical terminal event:
`completed`, `stopped` or `cancelled`. The caller consumes these through
`take_events`; that call is the explicit observation boundary for its mirror.
An explicit stop whose graph still owns voices retains its external owner
until logical retirement. Repeated stop and late callbacks do not duplicate
terminal events. A stop earlier than its queued admission is rejected before
submission. Stale accepted tokens never identify a reused owner.

`stop_all_and_wait` executes the checked bank stop command first, then cancels
native queued PCM, closes the device and joins the worker. Success requires
both source acknowledgement and actual device retirement. Native failures
propagate, including after cleanup. Destruction can cancel CPU owners and
retire native storage, but does not fabricate successful source stop.

`next_frame` and `earliest_command_frame` are rendering/command frontiers.
Queued PCM is ahead of the actual played position. The host must not use a
render frontier as proof that a sound was heard or naturally finished.

## Frontend integration

For packages with the ambient bank, runtime constructs and starts this owner
before intro and observes its first submitted PCM before beginning the film.
The worker initially renders silence; its clock continues through intro and
menu with unchanged idle RNG. Menu admission never resets that clock.

Prepared scene cue windows use the actual timeline sample index. The source
scan precedes the visual clock update and menu processing. The host stores
queued cue requests, then submits them during the audio pump, with callback
observation before and after submission. A dialog's leading pump and the
common tail pump share the same queued owners. Source-specific windows and
program mapping remain in prepared resources.

All native playback uses the shared Windows audio selector. Normal user launches
use the Windows default output; the previous local Oculus override was removed
at the user's request. Agent playback tests set `OPENRC_AUDIO_DEVICE_NAME` to
the exact Oculus Virtual Audio name and `OPENRC_AUDIO_DEVICE_REQUIRED=1` only
in their child process. Tests must not persist that override. An unavailable
explicit selection fails instead of falling back to audible default output.

## Verification and limits

Neutral unit and source-composition tests cover clock parity, irregular
buffer partitions, pending cancellation, completion, release, replacement and
hard stop. The real Windows device probe loads the twenty-resource neutral
diagnostic package and compares every submitted PCM sample against the neutral
player across original five-program admission, scheduled stop and replacement.
It also verifies future cancellation, an unstarted owner, a generic stop path
that retains a logical voice, and propagation of an injected native write
failure. The audited probe's `audio-program-bank-device-v3.log` passed on Oculus
with 24800 submitted frames, 13 events and byte-exact PCM.

These checks do not establish complete original wet-effect routing, absence
of additional original disc-stream owners, or completion of the normal
`transition/prepare` operation. That transition remains explicitly closed
until its remaining effect, stream and bank-lifetime obligations are met.

The runtime's `retire_frontend_audio` component stops the actual finite menu
bank and persistent ambient bank, consumes their terminal events, verifies
zero accepted/queued/physical owners and joined device retirement, then
releases both decoded banks. It preserves failure ownership instead of using
destructors as acknowledgements. The immutable prepared package remains
available; it is not the loaded bank's voice or PCM allocation owner.

`--smoke-stage frontend-audio-retirement` reaches the real intro, menu,
confirmation and New Game scene exit, requires active ambient voices at that
point, and exercises this component. It checks unchanged canonical session and
frozen pixels. It stops at the still-unregistered preparation gate and reports
`normal_sequence_barriers_executed=0`; this component diagnostic does not infer
effect or disc-owner completion from an empty native list.

The 2026-10-03 v13 runtime check passed on scoped Oculus output: two finite
menu sounds completed naturally; two physical and three logical ambient owners
were active before the stop. Three terminal events acknowledged the remaining
program owners, all seven started physical voices retired, and both banks were
released after actual native retirement. Frozen pixels and canonical revision
were unchanged. The log is `v13-audio-retirement-oct3-v2.log` in ignored
`local/forensics/frontend-transition`; the preparation gate remained incomplete.
