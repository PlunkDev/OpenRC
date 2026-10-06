# RAC scene animation compilation and source sampling

`rac_scene_animation_compile` connects the existing structural
`SceneAnimationBankV1` reader to the existing neutral `ActorAnimationBankV1`
and `ActorAnimationPlayer`. All RAC records and source update selection stay
on the compiler side of the prepared-package boundary.

## Actor tracks

`compile_rac_scene_animation_bank_v1` accepts selected actor ordinals, their
expected source class IDs and decoded bind rigs. Distinct ordinals remain
distinct clips even when their class IDs match. The output pins each neutral
clip to the canonical rig digest and preserves authored frame phase rates.

The scene sequence dialect has control bytes `00 ff ff` at sequence offsets
`+11..+13`, zero words at `+14/+18`, no trigger directory, and offsets relative
to its sequence start. `parse_rac_scene_sequence_v1` admits this dialect while
retaining the exact encoded bytes. Packed pose decoding shares the regular
sequence decoder; ordinary Moby and Ratchet trigger parsing is unchanged.

## Sampling an admitted source update

`sample_rac_scene_animation_tick_v1` implements the recovered general scene
consumer at `29a158` and `29a618..29a6bc` in the local SCES-50916 Veldin overlay:

| Output | Original selection |
| --- | --- |
| Camera record | `camera_base + 32 * chunk_update` |
| Current actor frame | `chunk_update >> 1` |
| Next actor frame | `current + 1` |
| Odd-update actor interpolation | `1` if camera byte `+0c` is nonzero, otherwise `0.5` |
| Even-update actor interpolation | `0` |
| Actor world position | Current root times `1-phase`, next root times `phase`, then vector addition |

Root arithmetic executes the existing integer VU numeric reference in the
original operand order: data first and scalar second for both multiplications,
then addition. Non-finite values and missing camera or next-actor samples are
rejected. `rac_scene_actor_playback_state_v1` projects this selection into the
existing neutral player; source phase `1` becomes next-frame phase `0`.
The camera-byte override is in the `BNEL` delay slot at `29a674`; that slot
executes only for odd parity and is annulled for even parity.

Camera positions and XYZ angles are read directly from the authored record;
camera samples are not interpolated. The X, Y and Z helpers consume radians.
The source forms its final camera basis from `-row2`, `-row0`, `+row1`, with
an optional source handedness override. The sampler exposes the angles and
does not claim to produce that final view matrix.

Camera record `+1c` is the horizontal half-angle tangent, stored at projection
owner `16cb40+b0`. The projection update at `1f7d00` multiplies this tangent
by float bits `3f418937` for the PAL selector or `3f466666` for the other
selector to obtain the vertical tangent. At `1f7ff0..1f8048` its horizontal
and vertical matrix scales divide viewport dimensions by the corresponding
tangent times near distance. This parameter must not be interpreted directly
as a vertical FOV angle. The compiler currently returns the original scalar.

## General scene chunk boundary

The original scene owner increments its chunk counter before ordinary
sampling. The first installed chunk therefore starts ordinary sampling at
update `1`. At update `80` for the PAL selector, or `96` for the other selector,
the owner installs the next chunk, resets its counter and samples that chunk
at update `0` in the same update. Full source chunks have `41/49` actor frames
and `81/97` camera records: the final camera and actor records are interpolation
guards, not permission to read the next actor beyond the array. Short final
chunks may also have a shorter camera array.

The general scene adapter samples one already-admitted update. It does not implement the
source scene owner, chunk I/O, subtitle/audio delivery, skip handling, the
final camera matrix or prepared cutscene publication. Normal per-actor phase
advancement does not replace the original scene clock. The resulting neutral
clips are usable through the existing animation resource and player; the full
Intro/Menu/New Game/cutscene path remains an integration task.

## Original frontend background

The boot executable has a distinct background owner at `18cc20` and consumers
`1eb338` (camera), `1eb458` (update), `204fc0/205220` (chunk installation).
`decode_rac_frontend_background_v1` reads its directory from the already-decoded
global frontend WAD. The directory base is header word `+04` plus word `+80`;
up to 70 eight-byte `(offset, byte-size)` rows address compressed chunks at
`directory + 800 + offset`, stopping at a zero size. The existing bounded WAD
decoder and `SceneAnimationBankV1` parser validate each source chunk. Caller
limits cover per-chunk input and aggregate decompressed output; metadata and
the last admitted interpolation samples must cover the complete source loop.

`RacFrontendBackgroundClockV1` executes the confirmed counter and reload order:
increment total/chunk counters, test the total duration first, otherwise reload
the next chunk at 96 updates. This frontend threshold is **96 for both video
selectors**, unlike the general cutscene owner. Initialization starts at zero;
the first ordinary sample is update 1, later chunk installation samples update
0, and the loop reloads chunk zero before sampling its update zero.

`sample_rac_frontend_background_tick_v1` shares root and skeletal selection
with the general adapter but follows the frontend's actual camera consumer:
projection is fixed to float bits `3f2147ae`, and the camera control byte does
not override the ordinary odd half phase. Calls at `1eb590/1eb5a8` execute
`VMULx.xyz` (`1f9c3c`, word `4bc20858`) and `1eb5b8` executes `VADD.xyz`
(`1f9be0`, word `4bc20868`). They are separate multiplications and addition,
so this adapter does not replace a multiply-accumulate with host arithmetic.

The original-source probe passed the audited portable executable with 15
chunks, duration 1398 updates, 715 compiled class-zero actor frames, 2796
camera/root/pose samples, 310356 joint transforms, 30 reloads and two loops.
All five source actor root tracks are sampled; that pose count qualifies only
the class-zero binding, not the other four actors' mesh or pose execution.
The subsequent complete frontend asset compiler now prepares all five actors,
75 clips and their eight texture bindings; see
[RAC_FRONTEND_SCENE_COMPILE_V1.md](RAC_FRONTEND_SCENE_COMPILE_V1.md) for its
separate full-source validation and boundaries.
The final chunk has 29 actor frames and 55 camera records; its final admitted
sample before looping is update 53. Synthetic tests cover exact directory
bounds, aggregate output limits, the frontend projection/parity differences,
the first update, the 96-update boundary and the short final chunk.

These are compiler-side consumers using the existing neutral animation
resource and player. They do not create a second runtime, publish a prepared
camera track, render the background models, or implement the title/menu owner,
audio events and input flow. In particular, the background camera and the
separate menu-object camera must not be replaced with the gameplay camera.

Source ranges, local fixture provenance and detailed owner observations are
recorded under ignored `local/forensics/rac-scene-animation-playback-evidence.md`.
The startup bitmap, original background model/texture bindings and transition
order are recorded in ignored
`local/forensics/rac-frontend-transition-evidence.md`. Original decoded source
files are under `local/forensics/frontend-transition/` and are never packaged
as runtime RAC data or committed as fixtures.
Tracked tests use synthetic data and cover actor identity, exact dialect bytes,
neutral pose sampling, source interpolation selection and chunk guard rejection.
