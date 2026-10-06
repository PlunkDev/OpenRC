# Original New Game movie lifetime

This source qualification refines the mandatory media consumers in
[FRONTEND_SEQUENCE_V1.md](FRONTEND_SEQUENCE_V1.md). It establishes original
operation order, ownership and audio command meaning. The source traces do
not establish native device completion; the Windows audio section records
the separate device regression.

The local SCES_509.16 ELF is pinned to SHA-256
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`.
The original extracted 989snd module is pinned to
`d482d7e0eb6fda6f95946f47630e0f35f3f7da89e554c47fdb42f5c5c8f636ed`.
Addresses below are compiler/source evidence, never runtime package fields.

## Movie entry and exit

`232920` chooses the original movie extent and language, aligns the two shared
work arenas to64 bytes, ORs bit8 into byte`13e6bb`, and calls`23b670`.
That callee owns the **entire synchronous movie**, including initialization,
feeding, ending and destruction. Calling it a preparation-only function would
hide most of its effects. After its return, the outer function performs:

1. `122598(0)`: await a video field.
2. `120858(0,0)`: await all source graphics pipelines becoming idle.
3. `123168(12f308)`: restore the normal video-field callback.
4. `1f4e08(4)`: the literal four-presentation fade already in the neutral flow.
5. OR bit16 into byte`13e6bb` at`2329f0`.

Both bit writes preserve other pending bits. Their audio effect occurs when
the original audio command pump consumes the byte, rather than at the store.

`23b670` stores the arena pointers at`161308/16130c`, clears stage/counter
`161314/161318`, obtains the current thread ID and changes its priority to1.
It sets stage1, calls setup`23bb90`, then on success sets stage2 and calls
feed owner`23b740`. It calls teardown`23be38` after either setup result and
clears both arena pointers. These pointers and kernel handles map to native
resource owners; copying their source integer values into SessionState would
not create those resources.

## Setup, ending and destruction

Setup`23bb90` initializes the encoded input ring, decoder, audio ring and
two-entry presentation queue. The decoder receives original video callback
`23c9c0` on stream type0/channel0 and audio callback`23caf8` on type3 with the
selected language. It creates/starts worker`23e1f8` with a16KiB stack and
priority1, binds the selected input extent, and installs/enables field
callback`23c7a8` and DMA completion callback`23c910`. Their returned handles
remain in the movie owner until teardown. The source input-extent binder
`23ce18` stores the two words and literally returns1.

Audio initialization`23bfa0` emits synchronous RPC`3b` with payload
`{1024,4096,1024,0,5,3}`. It supplies a real audio-stream owner, not an
acknowledgement flag. Decoder stream registration, worker creation and output
queue creation must all succeed before native media preparation completes.

Feed`23b740` continues pumping the audio ring and worker scheduling. Its first
ending loop retries`23e0d8` until four bytes of sequence-end data fit in the
compressed-input ring. The second loop checks`23e1b0` or decoder state3.
`23e1b0` first checks pending decoder input and then tests the internal decoder
word reached through`decoder+40`, at`12bb98`. These are actual data/state
conditions; neither original loop derives completion from elapsed duration.

At`23bac8` the source then:

1. Disables movie field presentation (`1612e0=0`).
2. Calls synchronous audio stop RPC`3d` through`23c0e0`, then clears its audio
   ring counters/state.
3. Enqueues RPC9 to restore volume group5 from word`13e6ac` and pumps audio RPC
   completion through`12ddc0`.
4. Clears stage/counter`161314/161318` and returns from the feed owner.

This is **not proof of natural completion of every queued audio sample**.
RPC`3d` reaches IOP`a198`, which mutes the B input and stops its block transfer,
or keys off its allocated voices according to the active transfer mode.
Replacing this source stop with playing every remaining audio buffer can
change skip timing. Native completion should stop/retire the movie stream at
the corresponding boundary and await actual callback/buffer retirement.

Teardown`23be38` performs this order:

| Original effect | Native ownership requirement |
| --- | --- |
| Wait input I/O, `120f30(0)` | Complete or cancel and join the actual input work. |
| Terminate/delete movie worker | Cooperatively stop and join the owned worker; no later producer may touch released buffers. |
| Disable/remove DMA callback | Retire the owned upload/completion callback. |
| Disable/remove field callback | Retire the owned movie presentation callback. |
| Destroy decoder via`23e008` | Stop decoder transfer, release its semaphore and decoder after its worker/callbacks retire. |
| Destroy audio via synchronous RPC`3c` | Release the actual movie audio owner and its buffers/voices. |
| Wait input I/O again; clear movie DMA mode | Establish retirement of remaining input work before releasing the owner. |

IOP RPC`3c` reaches`a0b8`: it calls the stop operation, releases the transfer
channel and allocated audio memory/voices, and resets its ownership fields.
The small callees`23cd28` and`23e5b0` are literal empty returns, and`23ce28`
literally returns1. They add no resource operation. This does not make the
surrounding cleanup consumer empty.

## Graphics and normal field clock

`120858` returns only when VIF1 DMA and GIF DMA active bits are clear, VIF1
status low2 bits are clear, VU control29 bit8 is clear, and GIF status mask
`c00` is clear. A completed Present call alone is not this condition. Native
cleanup needs actual completion of the final submitted movie GPU work.

`123168` operates on the display record at`132e40`. Its callback and handler
fields are at`+8/+c`. It disables/removes any previous handler before installing
and enabling a replacement; a null replacement clears both fields. The normal
callback`12f308` increments64-bit field counter`15ee48`, reads the hardware
timer and adds64-bit base`15ee40`, then stores the resulting time at`15ee50`.
It does not restore music or acknowledge GPU work. A native present-clock
subscription should resume only after the movie subscription is retired.

The syscall labels are checked against the primary
[PS2SDK syscall definitions](https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h).
Original instructions establish the call order and arguments above.

## The two audio bits control reverb

The source pump`22ddc0..22de4c` consumes byte`13e6bb` with this precedence:

| Pending condition | Queued original command |
| --- | --- |
| `flags & 8` | RPC`50`, payload`{2,0,0,0,0}` |
| Otherwise `flags & 13` | RPC`50`, payload`{2,mode,depth,delay,feedback}` |
| Otherwise `flags & 4` | RPC`10`, payload`{2,depth,12,3}` |
| None | No command |

The byte is cleared after selection. Here`13` and`50` are hexadecimal. The
all256-byte corpus proves precedence, payload width and unconditional clear.
RPC`10` is recorded for precedence coverage; its time-dependent effect is not
qualified by this document.

Original IOP direct/batched dispatch`16c8/1648` uses table`17bdc`.
Entry`50` is wrapper`1358`, which calls`c094`. Mask2 selects SPU2 core1;
`c198` sets effect mode/delay/feedback and`c560` sets left/right effect depth
from the same low16 bits. Original imports identify`libsd` ordinal23 as effect
attribute and ordinal5 as parameter writes; parameter IDs`b81/c81` are core1
effect left/right volume. Thus flag8 requests effect mode0/depth0 (reverb off),
and bit16 requests reapplication of the saved reverb settings. It is not a
music pause/resume pair. Import and field names are crosschecked with primary
[libsd imports](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/include/libsd.h)
and [effect/parameter definitions](https://github.com/ps2dev/ps2sdk/blob/master/common/include/libsd-common.h).

RPC9 table entry`484` calls`16300`, which clamps its signed volume argument
to0..1024, stores the selected group gain, and updates its active consumers.
The movie tail supplies group5. This is a group gain restoration, not a new
global volume default. The audio stream setup itself selects group5.

The six source fields below have no overlap with the47 primary and11×20
repeated reset-owner intervals at`1a05c0/1a08c0`. Their initial ELF bytes are
zero. Add canonical owners only in a coordinated schema revision, preserving
their live values through New Game and using the existing write dispatcher.

| Source field | Width | Meaning |
| --- | --- | --- |
| `13e6ac` | u32 | Current cached group5 volume |
| `13e6b4` | u32 | Saved reverb depth; consumer uses low16 bits |
| `13e6b8` | u8 | Saved reverb mode |
| `13e6b9` | u8 | Saved reverb delay |
| `13e6ba` | u8 | Saved reverb feedback |
| `13e6bb` | u8 | Pending reverb command bits |

The original initializer `22dbe8` loads configuration word `15eef0` at
`22dcb4`, stores it into the group5 cache at `22dd08`, then sends the cached
value through RPC9 at `22dd48/4c`. Its ELF configuration default is 1024;
`15eeec` defaults to 716 and `15eee8` to 1. Owner `209dc0` preserves these
three configuration words across reset. A loaded or user-modified
configuration must therefore determine the live group5 cache, rather than
its zero ELF seed. This initializer does not write the reverb fields above;
their zero file bytes alone do not establish every later live startup state.

The canonical owners are added through a coordinated schema revision.
Nonzero reverb needs an actual qualified audio effect implementation. A
success-returning consumer that only clears the pending bit cannot establish
that effect. These findings qualify control and ownership, not the full SPU2
reverb DSP or every command of the original mixer.

## Executable source evidence

Ignored diagnostic`local/forensics/trace_rac_movie_lifetime.py` executes the
hash-pinned original integer instructions over explicit synthetic state.
It checks256 pending-bit combinations, five language setup/teardown cases,
30 outer movie-order cases,64 graphics busy-gate cases, four callback
replacement cases, normal64-bit field-clock carry and two IOP reverb routes.
Syscall/library results and the main feed/drain owner remain named opaque
boundaries. The graphics busy inputs are synthetic; this is not a physical
device timing measurement or native consumer equivalence test.

Result`local/forensics/frontend-transition/movie-lifetime-source.json`:
`MOVIE LIFETIME SOURCE PASS`; SHA-256
`ab03cda30f4abaabba187ab8503b4b9005b89d01fc69e0300274c756375e7207`.
The report records exact source-range hashes, operation arguments, worker
descriptors, callback identities and the reset-owner overlap check.

## Windows movie audio retirement

`WindowsMediaAudioV1::stop_and_retire()` snapshots the actual played sample
count and natural completion before resetting the device. It checks
`waveOutReset`, unprepares the returned buffer, then checks `waveOutClose`.
Only a successful close reports `retired()`. Repeated successful retirement
is harmless; a failed ownership step remains available for retry, and
starting a stopped or retired owner is rejected. The borrowed neutral PCM
clip must still outlive the audio owner.

Windows reset returns pending buffers with `WHDR_DONE` and resets the device
position to zero, so those post-reset values cannot establish natural
completion or the final played clock. The saved values preserve that
distinction. Buffer unpreparation precedes device closure and release of its
storage. These semantics follow Microsoft's [reset documentation](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutreset),
[position documentation](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutgetposition),
[buffer retirement documentation](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutunprepareheader)
and [device closure documentation](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutclose).

The CMake-built and PE-audited `openrc-media-source-probe --audio-retirement`
mode passed with original decoded PCM on the actual Windows audio device:
one unstarted owner, three interrupted playbacks, and one naturally completed
50 ms original PCM prefix. It checked stable pre-reset sample counts,
idempotent retirement and rejected restart. Interrupted runs retained
600, 762 and 746 played samples and did not report natural completion.
The ignored result is
`local/forensics/startup-media/audio-retirement-source.log`; executable
SHA-256 is
`76d1573b9112581a391f415d8d3867163bb27cfcb95e535e2402e57ecfc0baf7`.
This tests PCM device lifetime, independently of movie decoder and GPU
retirement or nonzero SPU2 reverb equivalence.

## Windows cinematic storage retirement

`D3d11Renderer::retire_scene_for_media(token)` requires an admitted frozen
media frame and the current submission event already observed complete.
Later uploads, draws, resize work or scene admissions invalidate that
retirement eligibility. Missing, superseded and unobserved tokens are
rejected, as is an active gameplay owner. Successful retirement consumes
the token's eligibility.

`submission_drain_covers_current_work(token)` exposes that same coverage
check independently of presentation mode. `try_retire_scene_for_media(token)`
returns false, without changing owners, when newer work followed the current
observed-complete event. A caller that pumps window messages can then submit
and await a fresh event. Missing, superseded, unobserved or consumed
retirement tokens still throw. The caller returns after successful coverage
without pumping another message that could submit a later draw.

The method clears context bindings, releases cinematic geometry, actor
models/instances, overlay resources and their CPU container capacity, then
flushes deferred object destruction. It preserves the frozen media texture,
shared pipeline, device and swap chain. Microsoft's [context reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearstate)
and [flush documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)
establish the reference-release and deferred-destruction steps. The event
query establishes prior GPU completion; the final flush does not substitute
for that event.

The CMake-built and PE-audited `openrc-screen-overlay-d3d-tests` passed on
the actual device. It compared the frozen image byte for byte before and
after retirement; checked rejection before observed completion and after
new GPU work; checked disappearance of old actor, geometry and overlay
owners; recreated an overlay; and admitted the first prepared player on
the same renderer. The follow-up also verifies retry after a new upload,
paint draw and resize, preserving owners until a fresh event completes.
The existing exact integer composition corpus also passed. Result:
`local/forensics/frontend-transition/scene-retirement-retry-gpu.log`.
Executable SHA-256:
`8df2258e78882eb24ae4325aa0a3af8d9350919841a1eedc2aa5481a46a2db50`.
