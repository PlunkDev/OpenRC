# Neutral frontend sequence v1

Current native checkpoint, 2026-10-06: the runtime registers a real
`level/enter` consumer. This final continuation cue now executes and the player
reaches `complete` instead of stopping at `incomplete`.

Its preconditions match `level/admit-prepared-sections` after that
installation:

- installed level state and a completed admitted level owner;
- no gameplay session;
- no remaining frontend input, platform, card/movie presentation or audio owner.

The consumer then:

1. moves the one canonical `GameSessionV1` into
   `RuntimeGameplaySessionOptionsV1::frontend_session`, which rejects sessions
   with ticks, level requests or an entered level, and an additional
   `initial_persistent_state`;
2. admits the scene into the existing renderer with `set_gameplay_scene`;
3. reuses the developer `--level` admission and game loop code;
4. acknowledges completion only after gameplay exists.

The normal smoke executes 600 gameplay ticks afterwards and checks player
movement, retained session identity/bytes/revision and a non-frozen frame.
This consumer does not lower the source owners behind entry `2465f8`. In the
source, that entry is the whole level module: prologue, mode loop and exit
([LEVEL_ENTER_SOURCE_V1.md](LEVEL_ENTER_SOURCE_V1.md)). Entity admission,
camera and physics are explicitly reported as non-original. See `HANDOFF.md`
and `FIRST_PLAYABLE.md`.

Earlier native checkpoint,2026-10-03: the fresh PAL v14 continuation now
implements `transition/prepare` using the canonical prefix evaluator, verified
dry/resident audio profile, actual stop/join/bank release and GPU completion.
The final frontend framebuffer is preserved for the prepared initial fade.
Normal sequence smoke passes three cards, three complete movies, seven fades,
actual level loading, transition cleanup and `level/admit-prepared-sections`.
The latter takes the loaded content once into a durable owner and atomically
installs3,457 prepared state writes in the existing canonical session, retaining
the final framebuffer. At that checkpoint the remaining missing consumer was
`level/enter`; `level_state_installed=1` alone does not claim world entry or
playable Veldin. See `HANDOFF.md` and `STATE_INSTALLATION_V1.md` for the
executed scope and evidence.

`frontend_sequence` is a small linear prepared program and executable player.
It carries opaque resource IDs, types and exact payload SHA-256, a canonical
SessionState schema digest, explicit state writes, native consumer keys,
presentation clocks and button masks. It contains no disc offsets, source
addresses, executable bytes or RAC/PS2 readers. Encoding pins the complete
body digest; all counts, strings, writes and bytes are bounded before reads
allocate variable payloads. Resource admission compares against independently
decoded and hashed host resources before the first command.

`FrontendSequencePlayerV1::command()` identifies the next real operation.
The host dispatches the cue's registered consumer with its resource, level,
frame and duration. An unregistered consumer produces `incomplete`, and no
completion signal can bypass it. Registration is a host trust boundary: the
host must register actual implementations, never success-returning placeholders.

State writes run only through `apply_session_writes` on the existing canonical
SessionState, with matching schema digest and atomic ordered batch application.
There is no second mutable byte store. Generic acknowledgements cannot skip
these writes. Generic `current_level` supports a host level handoff where a
program actually needs one. The RAC fresh continuation does not emit that
consumer: its two level assignments are already complete canonical writes.
Original asynchronous preparation begins later at the explicit load call.

A fade advances only after each completed presentation. A loading cue checks
the actual presentation gate, then advances once per completed frame. While
its load is pending, every frame requires a real load-poll result. A pending
result extends duration to at least `frame + extension`; a completed result
stops further per-frame polling. End-of-program with pending loading remains
incomplete. `await_level_load` always requires actual I/O completion, including
when an earlier display gate prevented all card frames.

Media has separate start, feed, decoder-drain and presentation-drain phases.
End of prepared input, actual decoder stop or accepted input only requests
drain. None implies that the last video/audio was presented. Both subsequent
completion signals are mandatory before the next cue. These phases deliberately
do not predict completion from elapsed time or input size.

The host must preserve the frame/duration-dependent alpha of loading images.
A fixed precomposited overlay is insufficient when real pending I/O extends
the clock. This module neither supplies that image resource nor implements
unknown audio, resource preparation, teardown or gameplay admission consumers.
Passing the sequence tests is not full frontend or gameplay completion.

# RAC fresh New Game adapter

`rac_new_game_flow` runs the existing `224728` no-save executor with the complete
reset template. Every reached source write byte must map through a disjoint
compiler ownership interval into an existing stride-1 canonical u8 view.
The caller must supply actual current callback inputs and preserved values;
static defaults do not establish the current menu/card state. No input request
means no presentation program. The returned input writes then remain the actual
effects to apply. When a program exists, its first mandatory state cue contains
those writes: the caller must not separately apply them a second time.

The adapter requires the reset-written target, current level and fresh gate
to reach target0 / byte13de60==0. Unsupported save/overwrite branches remain
errors from the existing executor. Source input precedences remain there:
title mask0x840, action4 confirm0x40 after cancel gates, and no-save0x20 after
the actual card readiness and navigation gates. No replacement menu is created.

The qualified fresh branch is `233554..233604`. Its order is:

1. Original frontend exit/video restoration, L=-1, transition preparation.
2. Scaled fade6, card pair0/1 with scaled240, literal fade2, film0,
   movie cleanup, literal fade4, post-fade audio effect.
3. Card pair2/2 with scaled180, literal fade2, film1 and the same movie tail.
4. Write L=target0 before card pair3/4 preparation; prepare its resources,
   start the actual level load, clear the two source counters, present the
   card with scaled240 and real I/O polling, fade2, film2 and its movie tail.
5. Real final I/O wait, transition cleanup, prepared section admission,
   then actual level entry.

The card clocks are240/180/240 at60 updates/s and200/150/200 at50; initial
fade6 becomes5 in the latter domain. Literal fade2/4 and pending extension20
are not scaled. The compiler's source-only metadata retains the actual
seven presentation calls and card pairs. Movie TOC rows are1938+8i for
selector0 and1998+8i otherwise. Language values0/2/3/4/5 select audio directly;
card slot is max(language-1,0). No language1 fallback is inferred.

Normal New Game movies at L=-1/0 accept Start0x800 and the original held chord.
Mode-1 disables both; mode2 accepts any pressed bit. The prepared masks are
compared against the existing source skip executor across1,966,080 cases.

The ignored raw fixture executes850 original instructions across10 source
language/clock cases, including the exact timer instructions and delay-slot
L assignment. It records70 original presentation calls; opaque presentation
callees remain boundaries, not claimed transitive executions. SHA-256:
`59fcfde702e182fcb0c591c8923b8a04abf731f8cbb9e76f8ef941f51d0fb2ab`.
The permanent flow target accepts this optional fixture path for native
comparison, alongside synthetic current-state/write/codec/order checks.

# Live input and complete shared state

`FrontendNoSavePlanV1` is the neutral live callback contract. It binds every
read and write to named stride-one byte views in the existing SessionState;
screen and sound-object references are host-issued handles. Its codec admits
bounded records and verifies the complete payload digest. Every invocation
reads current focus, input, card readiness, navigation, selection and preserved
settings from canonical storage. No future callback snapshot is compiled.

`evaluate_frontend_no_save_v1` accepts const canonical state and returns the
ordered write batch with its expected revision. Existing session owners commit
through `apply_persistent_state_writes` before acting on a request or sound.
`execute_frontend_no_save_v1` is the direct SessionState wrapper. Unsupported
card-save/completion, slot-confirm and preserving-reset arms return an explicit
reason with no mutation. Missing/invalid views and stale revisions fail before
committing. Independent source owners cannot alias canonical bytes during
compiler lowering. The current permanent differential corpus has 1741 cases,
including 98 reached fresh resets, plus schema/codec/revision rejection and
changes to preserved values between preparation and live invocation.

`compile_rac_new_game_continuation_v1` prepares only the presentation tail.
It includes no input/reset cue. The host starts it only after the live adapter
commits a New Game request. `compile_rac_new_game_flow_v1` retains its original
snapshot/execution contract by prepending that invocation's actual effects to
the same continuation. Neither path replaces the card lifecycle with invented
ready values.

`compile_rac_frontend_state_v1` reads the admitted ELF and actual reset record
from the original global catalog. Every one of 267 descriptor copies gets a
named primary-field or level-row owner, with no memory-sized backing image.
The existing `rac1.progress` selector/bit/registration views alias those same
owners. Additional frontend fields have their own named ownership. Initial
bytes are the actual ELF load image, including explicitly declared zero-filled
segment tails; these are boot seeds, not a claimed reached menu/card snapshot.
Source pointers are translated to neutral screen handles at the compiler
boundary. Runtime consumers must update actual focus, previous-result and card
state in these same views as their owners execute.

The helper returns three neutral package resources: `frontend/session-state`
(`openrc.session-state`), `frontend/no-save-input`
(`openrc.frontend-no-save-input`) and `frontend/new-game-sequence`
(`openrc.frontend-sequence`). Their decoders are the established SessionState
initial decoder, the neutral input decoder and sequence decoder. Neutral
`frontend_session_state_limits_v1()` supplies the bounded state admission
profile without any compiler header. Optional ISO/ELF arguments to the input
test compile this actual package and compare the complete live reset byte for
byte against the existing source executor. Controlled ready values in that
test are test inputs, not proof of the real card lifecycle.

Required consumers with presently unclosed transitive source effects include
`frontend/exit-and-video-restore`, `transition/prepare`, `loading/prepare`,
`loading/begin-presentation`, `media/prepare`, both `media/cleanup-*` stages,
`transition/cleanup`, `level/admit-prepared-sections` and `level/enter`.
The source preparation calls at2333c8..2334ac include audio ownership,
I/O teardown and display configuration. The loading prologue calls232b90,
122598 and2348e8. Its presented-frame tail includes209e68/209070 before
204c60 polling. The movie tail calls122598,120858,123168 before fade4,
then modifies its audio flags. Final120f30 and2350a8 precede resident
12db18/12da38 section admission and entry2465f8. A real runtime lowering must
account for these owners; registering a no-op does not close them. The native
`level/enter` consumer is a real neutral gameplay admission, not a no-op.
It still leaves the transitive source effects of `2465f8` open.

The display boundaries have concrete ownership, not abstract acknowledgements:
`122598` synchronizes the video field; `120858` polls until the GIF/VIF/VU
transfer owners are idle; `123168` replaces the active vertical interrupt
callback; `2348e8` establishes the two render-command buffers and active cursor;
`2350a8` removes two transfer interrupt handlers and clears their identities.
The native host lowers these to synchronized presentation, actual GPU work
completion, active presentation callbacks and real submission resources.
`Present(1)` alone establishes neither complete GPU idleness nor decoder/audio
drain. The media preparation/cleanup consumers own per-clip decoder and audio
objects; the original intro's local objects cannot silently remain the active
owners of later clips. Actual package loading starts during the third card,
while `level/admit-prepared-sections` and `level/enter` remain later boundaries.

The native `transition/cleanup` consumer now requires completed level loading,
retired movie/card/frontend presentation owners and retired frontend audio.
It awaits current GPU work and releases the actual D3D11 completion event.
New work submitted while pumping window messages requires a fresh event.
The event identity becomes invalid after retirement; the frozen frame, shared
device and canonical session remain available for level admission. The GPU
qualification checks stale/unobserved events, intervening upload/draw/resize,
unchanged pixels and subsequent renderer use. This consumer remains behind the
separate, unimplemented `transition/prepare` gate; its registration does not
establish the full normal New Game flow. The media-library diagnostic exercises
the final graphics operation without acknowledging the normal sequence barriers.

The neutral `frontend_menu` callbacks now cover the actual initial no-card
route. `bootstrap` commits the original startup's new-game context. Each
source frontend update first runs the card owner and UI owner together through
`frontend_card_command_v1` / `evaluate_frontend_absent_card_v1`, using a result
from a real host poll/query. Missing, pending, accepted and completed-absent
remain distinct; an elapsed frame does not fabricate completion. The original
sequence is polling209e68, UI209070, controller218908, then mode dispatch1eb300.
Capture the mode at update entry and execute exactly one title/menu/dialog arm.
In particular, the title's pressed40 event must not also select New Game in
the same update.

`title_input` only commits the original root-phase45/mode3 request.
`evaluate_frontend_main_entry_v1` waits for the host's actual prepared actor,
animation, camera and submission-resource admission, including14 distinct
nonzero object handles. Its first admitted call starts the original reverse
same-screen transition;12 further source updates commit main phase46 and
derived focus. The named current-screen and transition-remaining owners retain
the source state instead of a host countdown. The action4 callback then enters
dialog kind3 on a live confirm press. Existing main node initializer and cleanup
references are zero; inserted-card/save or other screen branches stay explicit.

The absent backend drives UI mode0→3→4. It never supplies the ready1/16 or
inserted-card-type2 values required by the separate no-save slot screen.
In dialog mode4, the original fresh-context confirm40 executes the complete
267-copy reset, live preserved-settings restoration and actual New Game request.
The56-case original card/dialog fixture compares source control writes and six
reached fresh requests; the reset boundary reuses the independently checked
transitive reset owner. Its injected type0/result-1 combination is an explicit
control input, not proof of a physical no-card result. In particular, result-1
means a formatted-card change and the original completion owner retains busy2.
An unavailable-card completion must carry its actual negative result through
`FrontendCardSignalV1::completed_result`; the neutral owner does not substitute
a return code. Original mcman/mcserv no-device execution qualifies result-11,
type0/free0/formatted0. The sequential five-flow fixture runs725 original
operations (71,999 instructions) with one state per flow, covering different
poll phases at action4 entry. Result-11 releases busy3→1→0 and permits the
panel after the original eight-update delay; the result-1 control stays busy2.
This corrects the earlier backend's conflation of these outcomes without
changing the prepared state schema or forcing a UI-mode transition. The
original modules are recorded in ignored `card-no-device-source.json`; the
public result naming is also documented by the
[PS2SDK memory-card sample](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/memorycard/samples/mc_example.c).

`evaluate_frontend_dialog_presentation_v1` is read-only. The original draw owner
emits black coverage48/128 before the draw-delay gate. Kind3's panel additionally
requires zero card-busy and signed age>0. The source body and button prompts
use different remaining counters: duration and fade. The returned indices
select compiler-prepared samples; they do not authorize replacing source
COP1/VU arithmetic with a host linear fade. The288-case presentation fixture
executes the actual branch and keyed-message selections up to the explicit
numeric/layout boundary. It does not qualify the downstream dialog raster.

`compile_rac_frontend_menu_resources_v1(ISO, ELF, decoded_frontend_WAD)` prepares
the actual main actor, animation,13-sample timeline and original three-label
RTTs into `frontend/menu/actors`, `frontend/menu/animation`,
`frontend/menu/timeline` and `frontend/menu/lists`. All four use existing neutral
formats. The list overlay contains12 invisible transition frames followed by
the eight PAL focused-row color samples and a final hold. The source slot6
visibility gate is retained. Source class, font, localized text, projected
bounds and GS upload extents are extracted directly from the admitted sources;
no forensic input files or replacement labels are needed. Decoration remains
additional integration work.

The same helper also emits nine original absent-card dialog overlays:
`frontend/dialog/backdrop`, `frontend/dialog/body/{empty,absent,without-game,
unavailable}` and `frontend/dialog/prompts/{new,existing,without-game,
unavailable}`. The backdrop has one coverage48 frame. Body and prompt layers
have26 samples indexed by their independent remaining counters0..25. The
seven source panel rectangles and each original font1 glyph retain draw order;
they are not flattened on transparency, which would change integer blending.
The source1f55c0 backdrop disables alpha testing before the dialog glyph pass;
the main-list RTT's separate GEQUAL4 test is not inherited here. The source
layout, glyph, complete quad and panel-call fixture checks459 original draws
and the logical unit-sampling rasters at remaining0. Its54,860 layout,
23,432 glyph,41,710 quad and324 panel instructions are execution evidence;
the downstream logical raster is a reference, not physical-console capture.
`rac_frontend_menu_overlay_limits_v1()` admits the complete4096-image bounded
dialog profile and must also be supplied to CPU compositing when applicable.

`evaluate_frontend_title_v1` reads the admitted idle title's original60-phase
prepared pulse while keeping counter/logo/prompt values in the shared state.
Mode0 updates the original logo ramp and pulse; mode3 writes the source
counter50 and subtracts16 from both alphas, retaining clamp write order. Mode4
holds these values. `frontend_title_presentation_v1` selects the current
original alpha slices for drawing after the scene/main and before dialogs.
The v11 source-owned title fields made347 buffers,448 views and57,663 bytes;
no auxiliary title-state mirror is needed. The864 finite title updates compare
the neutral owner with the existing source-qualified title reference.

`exact_native_menu_flow_profile_v1` is compiler/publication validation for
30 appended resources in the v12 profile. It checks neutral decoders, bounded provenance sets,
actor/animation/state/resource digest links, entry and loading clocks, complete
267-owner reset coverage and the exact39-cue prepared continuation. Its
structural synthetic publication fixtures are not original-asset evidence.
Existing native preparation additionally recompiles the complete shared
package from the actual admitted image before accepting cache identity.

The initial main actor hold is source-owned. An additional original-instruction
trace runs64 consecutive screen/animation/stop updates:2,336 instructions in
the screen stage and28,097 in animation/stop, recording896 object states.
Only slots2/3/4 select seven-frame sequences16/17/18; the other eleven slots
select one-frame sequences with rate zero. At sample12 the reverse animation
wrap selects frame0 with phase1 and writes speed0. Later updates only clear
the transient wrap flags; pose state is fixed from12 and complete recorded
state from13. The existing timeline compiler already rejects a final sample
whose speed and rate are both nonzero. No additional active animation suffix
is needed for these14 objects on the initial main route. This does not qualify
input-driven screen changes, focused-row colors, decoration or other owners.
The ignored `frontend-main-active-source.bin` fixture is47,888 bytes,
SHA256`8ac7ae0b9081e5aa69be750b1db480a0defaf6a59207411035c9502064b0502b`.

## Transition owners and prepared feedback, native profile v12

The canonical schema now has358 buffers,461 views and57,690 bytes. Ten new
source owners cover `display/video-selector`, the saved display byte,
`loading/selector`, `collision/query-flags`, `audio/group5-volume`, and the
five `audio/reverb-{depth,mode,delay,feedback,flags}` fields. Their source ranges
do not overlap any of the267 reset owners. Two loading-selection byte views
alias offsets8 and14 of existing `progress/primary/field-14`; there is no copy
of those progress flags. A derived startup selector is a separate prepared
configuration field, not a replacement for the live display owners.

The compiler reads original disc sector289 byte51 and applies the original
`201f74..201f90` comparison with'N'. It rejects presentation selector/cadence
disagreement. `evaluate_frontend_platform_bootstrap_v1` commits that admitted
selector, the original group5 cache copied from preserved configuration
field36, the later saved selector byte and collision flag clear. The file's
zero display/audio seeds are not treated as the reached live state. Reverb
fields retain their live values: no reached nonzero startup writer has been
established. The earlier menu `bootstrap` remains a separate API for the
earlier published frontend schema.

`evaluate_frontend_exit_v1` first checks the actual committed transition
request. When reached, it compares the live and saved display selectors. A
mismatch returns the ordered restore write and a required display operation;
equal selectors do not manufacture device work. Original restoration is at
`1e9dc8..1e9e1c`, after the frontend loop returns. The loop's `1ebfc0` is only
its return-gate load. Native frontend storage retirement remains real host
work before acknowledging the exit consumer.

`evaluate_frontend_transition_prefix_v1` covers `233308..2333c8`, before the
first external call. It ORs the collision high bit, writes frontend mode6,
then selects loading0/1/2 using the two aliased progress flags and signed
target thresholds8/14. It preserves the intermediate source writes. This
does not complete the remaining audio, loading, graphics or resource-owner
operations inside transition preparation.

The original instruction fixture checks10 startup projections,12 exit gates
and324 loading-selector cases, including negative signed targets and live
reverb preservation:13,744 instructions and346 native comparisons. The
ignored97,718-byte fixture SHA256 is
`e61124437a95a6a0d7159236f6e0be94e7aded1c684be731205d6a6cc2f7f06a`.
Unowned startup audio groups and collision pointers are explicitly outside
the projection; their instructions execute, but their native consumers are
not claimed by this evaluator.

`compile_rac_frontend_fade_v1` emits neutral `FrameColorTransferSequenceV1`
tables for original `1f4e08`. The source waits one field before its body and
one afterward, even for a zero-length source body. The reached positive
durations are bounded before allocation. Each body update applies its table
to the previously completed framebuffer, preserving byte quantization:
PAL initial5 has black coverages26/32/43/64/128; literal2 has64/128; literal4
has32/43/64/128. The60Hz initial6 starts22, then follows the initial5 values.
The existing startup bitmap compiler uses this same qualified helper.

The New Game helper appends `new-game/fade-{5,2,4}` at50Hz, or replaces5 by6
at60Hz, to its original six card/movie resources. Each uses
`openrc.frame-color-transfers`, one lead update, one tail update, and a table
for each original body update. Fade cues reference those three exact resource
digests. Earlier neutral sequences may omit fade resources; the v12 native
profile requires them and compares every table and timing value. The host
must finish both surrounding waits before advancing beyond the cue.

The original fade graph executes1,176 instructions over durations0/1/2/4/5/6,
asserting complete lead/body/tail external-call order. The positive-duration
fixture compares4,608 transfer bytes. Its4,680-byte ignored file SHA256 is
`d9a55963f3c32ff929e9923344a0784f024229a075a193c78aeb70f56c426a62`.
The two source level assignments remain ordered state cues; removing their
duplicate host barriers yields39 continuation cues. It does not move actual
level loading earlier than `232f54 -> 12f4a8`.

The v12 profile also validates five original menu `openrc.audio-clip`
resources, `frontend/audio/variant-{0..4}`, with finite48kHz stereo PCM and
matching bank/module source provenance across all five. They retain the
compiler's effects1024/stereo contract. Source-recompilation equality and
native bank admission remain separate checks.

The final compiler/publication fixture accepts all30 appended resources in
the37-resource shared package and rejects59 missing or altered profiles.
Actual ISO/ELF preparation of all nine New Game resources and five menu clips
passes their neutral decoders; the full named-state/source input corpus also
passes with the358-owner schema. These are compiler and bounded owner tests,
not a claim that all transition consumers or the complete playable flow have
already been implemented.
