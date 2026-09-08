# Original frontend quad and submission scope

These compiler-side components continue the original
[text-layout and glyph owners](RAC_TEXT_LAYOUT_V1.md). They emit bounded original
command bytes; they are not a runtime PS2 decoder, a replacement menu or a
claim of complete GS raster behavior. No new prepared resource is introduced.

## Floating quad owner

`rac_float_quad_source_steps_v1` describes all 22 ordered operations of original
owner `1f5bb8`, including its `1fa898` conversion leaf. Inputs X, Y, width and
height remain symbolic raw single-precision values. The sequence retains four
single multiplies, two single adds, four word conversions, four separate live
screen-offset reads and the original wrapping word additions. Far corners use
ADD then MUL, not reassociated arithmetic. All conversions and reads precede
the integer culling gates. This component does not evaluate COP1, choose an
FCSR mode or certify exceptions and numeric results.

`emit_rac_float_quad_v1` consumes the four explicit converted words and four
explicit screen-offset words. It applies source word wrapping and ordered
signed culling, then emits the complete 144-byte CNT/DIRECT/REGLIST packet or
no packet. Equality at the source boundaries survives, and an inverted
rectangle is not normalized. The packet preserves:

- raw low64 TEX0 and RGBAQ, with no early RGBA8 conversion;
- PRIM, region CLAMP and its original inclusive endpoint calculation;
- top-left, top-right, bottom-left, bottom-right UV/XYZ2 order;
- raw64 atlas origins, word-sized endpoint additions and sign extension,
  including source OR pollution for unusual signed/wide inputs;
- the final CLAMP reset, exact REGLIST descriptors and final upper64 padding.

The source's three intermediate command-cursor publications are reported
relative to entry. The separate owned append API checks capacity and alignment
before copying the entire packet; rejection leaves storage and cursor unchanged.
That safe wrapper does not pretend to reproduce intermediate memory visibility.

An independent tracer executed the complete original emitter and conversion
leaf, recording symbolic COP1 operations while supplying explicit conversion
outputs and live offset reads. The audited native comparison passed **4,133
cases**: 1,974 emitted packets and 2,159 ordered rejections. It compared every
packet byte, all four coordinates, all 22 operation steps, cursor publications
and guarded append behavior. This is source integer/ordering equivalence, not
numerical or visual PS2 qualification. Raw traces, source identities and
reproduction fixtures remain under ignored `local/forensics`.

## Integer RTT composite quad owner

`emit_rac_integer_quad_v1` implements the complete scalar `1f5800` callee
selected by the enclosing owner's RTT composite call. The first eight source
arguments use their low words; RGBAQ and TEX0 preserve all 64 bits. Two actual
screen-offset reads, Y then X, are each reused for both corners of that axis.
Endpoint additions wrap before shifting. UV fields are added, not ORed; XYZ
packing retains the source sign-extension/OR behavior. There is no COP1
arithmetic, CPU culling, CLAMP write or normalization in this source function.
It always emits the complete 128-byte CNT/DIRECT7 and REGLIST11 packet,
including the final upper64 padding and relative cursor publications 16/32/128.

`emit_rac_frontend_composite_packet_v1` connects the actual typed `21abf0`
owner call to this emitter with explicit post-restore offset observations.
It rejects a different call kind/site or a nonzero callback pointer. This
executes that callee, not preceding opaque callbacks, projection or restore.
It is never used as a replacement for the original floating glyph path.

Five permanent test groups check packet/register/strip output, full raw
argument bits, overlapping UV fields, signed wrapping, no invented culling,
bounded atomic append and the actual owner-to-transport-to-GIF connection.
An independent tracer executes all 97 original instructions in each of
**3,200 cases**. The audited native probe matched **409,600 packet bytes**,
the cursor publications and guarded appends. Synthetic raw arguments and
original instructions establish source equivalence, not hardware rendering
or correct inherited sampler/texture state.

## Original callback and cold texture batch

`rac_frontend_gs_scope.hpp` owns the source callback ALPHA/TEST preamble,
SCISSOR writer, cold per-batch texture bindings and reordered upload/draw
submission. Inputs remain explicit source-address regions, live allocator and
screen words, ordered texture IDs, live upload gate and incoming GS state.
Source addresses never become host pointers or runtime package fields.

The recovered owners include begin/bind/end `1f4630`, `1f4868`, `1f4748`,
transfer `20c2f8`, SCISSOR `234d58` and flush `234e80`. The New Game callback
writes ALPHA_1 followed by TEST_1 before its batch. This helper represents the
path that reaches begin; the original callback can return after those two
writes without beginning a batch. Whole-node selection now belongs to the
separate [original text-node callback](RAC_FRONTEND_TEXT_NODE_V1.md), not this
submission helper.
Its SCISSOR writer clamps
against the caller's live dimensions while retaining source signed arithmetic
and OR behavior; it neither normalizes an inverted rectangle nor masks away
sign-extension effects.

Every begin uses a cold descriptor cache. Repeated binds of one source ID reuse
that batch's TEX0, while different IDs remain different allocations even when
their source images alias. Palettes precede pixel planes, and allocator order
follows first bind order. The accepted lossless domain is explicit: at most 64
descriptors, source relocated offsets fitting halfwords in quadword units,
256-byte GS allocation granularity, bounded transfer dimensions/IMAGE counts,
and allocations contained in the 4 MiB GS address space. All source reads have
bounded ownership and all caller plans are revalidated before emission.

The original NEXT chain deliberately makes memory order differ from execution
order. The draw block occupies memory before the upload suffix, but submission
executes the suffix first and returns to draw afterward. The output preserves
the real CNT/NEXT/REF records and separately reports execution-order segments
and external read ranges. The final continuation is one-past-output, not a
license to dereference it. Caller draw input may contain only owned complete
CNT/DIRECT packets, never arbitrary external references or jumps.

The live zero upload gate emits no upload references and does not establish
texture residency. With the gate enabled, even an empty queue retains the
original flush REF: TEXFLUSH followed by the source TEX0 write. That second
write carries CLD behavior and must not be optimized into an unrelated bind.
Cold-font cache handling does not model begin's unrelated level-material cache
invalidations.

Missing incoming GS registers remain unknown. In particular this callback does
not establish TEX1; source ELF screen/frame data is not evidence of current
live render-target state. Complete inherited GS state, image transfer/residency,
CLUT latch and raster consumers remain separate integration requirements.

A separate actual-instruction tracer and audited native probe matched **79
cases, 4,859 bind calls, 2,534 cold uploads and 308,352 command bytes**. This
compares complete TEX0 results, allocation/queue state, SCISSOR output, all
CNT/NEXT/REF bytes and the flush literal. Cases include all 40 original catalog
rows, repeated/aliased IDs, both upload gates, the 64-entry queue boundary,
narrow transfer buffer widths and signed screen/clip edges. Eight permanent
synthetic test groups additionally check malformed plans, bounded ownership,
unknown incoming state and failed output requests. These are original
instructions over explicit synthetic live globals, not a console/live-menu
capture, DMA-device execution or proof of texture sampling.

## Enclosing original draw owner

`plan_rac_frontend_owner_v1` retains the reached control flow of `21a610` as
ordered calls, raw writes, emitted packets and explicit numeric dependencies.
Separate observations cover the initial object pass, 14-slot projected-bounds
prepass, both callback passes and the final object pass. Source gate precedence,
slot-six gating, direct/RTT selection and callback-return crop precedence are
preserved. The final pass does not invent a null-object skip that the original
does not perform. A callback pointer reread after RTT setup/clear may differ
from the pointer that passed initial selection.

The RTT path emits the complete 240-byte setup and clear packets, retains live
frame/depth/allocator values, and represents both viewport setters as ordered
26-effect plans. Converted values and multiplication remain symbolic; source
call delay-slot stores execute before their callee's conversion. Projection
`1f3140` is a typed unexecuted dependency, not a host-math approximation.

Restore reads the actual command-cursor state. A nonzero value selects REF9 to
exactly 144 caller-owned bytes at the masked live environment address. Zero
selects the original immediate-environment call with its unmasked argument;
that source callee is recorded, not falsely claimed to have executed. Display
dimensions are read live when reconstructing the viewport. This is not a stack
that restores arbitrary guessed incoming GS state. Reached source ranges and
finite output/dimension domains are validated; unsupported inputs fail instead
of being normalized into a different render target.

Eight permanent unit-test groups cover phase freshness, skip precedence,
projected writes, RTT setup/restore, return flags and signed/wrapping crop,
viewport ordering, both restore branches and input/budget rejection. The
bounded owner does not execute object rendering, camera/projector callees,
menu callbacks or the composite quad call while constructing that plan. The
separate integer composite adapter above now executes its genuine callee;
other real effects, the complete menu lifecycle and the renderer remain
integration work.

An independent actual-instruction tracer and audited native comparison matched
**48 cases and 10,711 ordered effects**, including 272 direct callbacks,
698 RTT callback plans, 420 composite calls, 347,200 emitted packet bytes and
36,296 symbolic viewport operations. All reached gates, argument words, raw
writes and complete packet bytes were compared. Opaque source callees use
explicit observations/hooks in this diagnostic; the result is not a running
original menu, a hardware numerical capture or an executed graphics device.

## Shared PACKED and REGLIST consumption

The existing bounded GIF/GS decoder now accepts both register formats. REGLIST
dispatches natural 64-bit GS values through the same register consumer used by
PACKED A+D, not by fabricating PACKED vertex fields. It concatenates all
`NLOOP * NREG` half-qwords before rounding to packet qwords, handles NREG zero
as sixteen, ignores REGLIST PRE/PRIM and treats REGLIST A+D as NOP. Only the
final unused upper64 is padding. Raw qword and selected half are retained as
write provenance. Unknown data stays unknown and all stream limits remain
aggregate limits. The strict XGKICK entry point remains register-only; the
shared linear stream entry point below additionally retains IMAGE payloads.

Synthetic regressions exercise all 64 NREG/loop combinations, both halves,
odd counts crossing loop boundaries, padding poison, natural register fields,
partial-known data, malformed framing and state across formats/tags/events.
An integration regression uses actual quad and SCISSOR output inside the actual
callback scope, then decodes its PACKED/REGLIST drawing stream. It checks two
independent strips, colors/Q, UV/XYZ, inherited clipping and CLAMP reset. Missing
PRMODECONT stays unknown; the known-state case provides an explicit synthetic
incoming write. The immutable packet adapter used by this test is not a claim
that EE PATH2 submission has become VU PATH1 execution or that the GS has
rasterized those commands.

## Actual transport and continuous GIF input

`read_rac_frontend_gs_stream_v1` follows the actual command bytes through the
source frontend's canonical CNT/DIRECT, REF/DIRECT and zero-payload NEXT forms.
It does not trust the emitter's execution-order annotations. Explicit immutable
source mappings must be aligned, disjoint and bounded; references must resolve
wholly within one owner. NEXT targets stay inside command storage or terminate
at its declared continuation. Aggregate source, command and output budgets
bound malformed/cyclic input. Unsupported DMA flags, commands and VIF forms
fail explicitly. This is the recovered frontend transport domain, not a full
DMA/VIF device or permission to dereference source addresses in the runtime.

All reached payload bytes are concatenated before GIF framing. In particular,
an IMAGE header in one DIRECT can own texture data delivered by the next REF.
`decode_gif_gs_linear_stream_v1` uses the existing register/primitive consumer
continuously across PACKED, REGLIST, IMAGE and EOP boundaries. IMAGE bytes are
owned output with exact tag, stream offset and preceding-register-count
provenance. They are not interpreted as tags or applied to VRAM. Nonempty
unqualified format 3 is rejected; empty tags ignore PRE and payload descriptors.
The final input must end at EOP, but intermediate DIRECTs need not do so.

Eight permanent test groups cover real batch/quad output, both upload gates,
ownership and budgets, malformed transport, split GIF payloads, IMAGE framing,
state/strip continuity and the enclosing RTT packet subsequence. The latter
checks the three-tag 240-byte setup, non-EOP clear and exact caller-owned REF9
restore; it does not execute opaque callbacks/projectors in the owner plan.

The original 79-case instruction-generated command corpus also passes the
transport and shared decoder: **5,744 DMA tags, 14,665,216 GIF bytes and 2,594
IMAGE payloads containing 14,446,848 bytes**. Register/tag counts and ordered
image bytes/prefixes are checked against the original upload queue. Texture
contents in this transport diagnostic are explicitly generated test bytes;
this is not a captured live GS upload or hardware/raster qualification.

## Remaining frontend work

This is **PARTIAL original frontend integration**. Qualified EE numeric
evaluation, enclosing original render state and its live ownership, native
render integration, original menu lifecycle/input/transitions, intro/audio and
normal New Game are still required. Component source comparisons and the
existing direct-level smoke path do not satisfy a normal startup-to-Novalis
playthrough or its separate fidelity pass.
