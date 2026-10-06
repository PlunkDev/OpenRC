# Initial TIE and shrub lighting

The compiler-owned `rac_instance_lighting` API produces the original packed
64-color TIE or 24-color shrub palette for one authored placement. The runtime
receives neutral vertex colors only. Source selectors, class normals, VU
instructions and original placement records remain on the compiler side.

This qualification uses SCES-50916 ELF SHA-256
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`
and frontend WAD SHA-256
`159e6341c3180831940350097bbbbf2115b9d35e4a030a856d563206b462c2d2`.
All original bytes and source comparison fixtures are ignored local files.

## Original palette consumers

Resident VU0 overlay 436083 has LMA `100af0`, VMA zero, and size `580`.
The shrub consumer `22b8f8` enters at byte addresses `0` and `160`; the TIE
consumer `238688` enters at `2c0` and `420`. Each entry processes four normals.
The class fields `+2c` (shrub) and `+0c` (TIE) identify signed XYZW halfword
normal arrays with 24 and 64 records respectively. The compiler executes the
original arithmetic through the shared bounded VU executor.

The source loader `1ea104..1ea174` first clears sixteen directional-light
records, then copies at most twelve authored records. Initial placement
point-light selectors are explicitly `ffff`. The low four bits of the
directional selector choose a record; its nonzero high-byte blend uses the
source normalization and ordered arithmetic. Later dynamic point lights are
outside this initial-state API.

Shrub basis normalization uses the original reciprocal square root value
operation. TIE loader `1f9cb8` computes each basis length through VU SQRT and
then EE DIV.S before `238688` uses the stored reciprocal. Both retain ordered
MUL/ADD/MADD and separate accumulator overflow state. Shared value helpers use
integer reference arithmetic; no host square root is substituted.

Shrub owner `1ea854..1ea880` reads full 32-bit placement words at `+50/+54/+58`
and combines them with shifts 0/8/16 plus `80000000`. Reading only their low
bytes before packing changes overlapping bits and is incorrect. TIE colors
come from 64 RGB1555 halfwords at placement `+50`; PEXT5 shifts each five-bit
channel left three, without bit replication. Output packing uses original
PPACH/PPACB low bytes and retains the GS alpha scale of 128.

## TIE vertex binding and explicit LOD0 lowering

EE `237cd4..237d28` reads packet-table entry `+0a` as a 16-byte relative offset
from packet data, and `+0b` as the number of four-byte words in one lighting
index stream. The second stream begins after the first stream rounded up to
16 bytes. The original first stream contains one byte per encoded dinky
vertex, zero alignment to four bytes, then `{C0,C1,C2,ff}` for each fat vertex.
Meaningful primary indices are 0..63; secondary indices are primary plus 64.
The zero alignment and `ff` sentinels stay unchanged in the second stream.

`RacTieVertexV1` preserves index count 1 or 3 and all source indices before
sorting or duplicate GS-write expansion. Count zero explicitly identifies a
historical synthetic packet with both stream fields absent; it never supplies
an invented color index. Fat vertices also preserve the first three signed
halfwords as `quantized_morph_delta`.

VU overlay 224979 loads colors from local memory base `346`. The original VIF
template at `160b20` selects STMOD1 with the `4b000000` ROW seed. Fat arithmetic
at `11f0/11f8` first computes the half-weighted C1/C2 pair; `1220/1228` then
combines it with C0 using the original raw float words. EE `237770..2377a4`
supplies `{0,0,256,0}` for high detail. The packed color is exactly C0 at this
state; the geometry uses the base position because the morph factor is zero.
During transitions the source weights are `{t,0,256*(1-t),256*t}`; ordinary
byte averaging is not a replacement for the original ordered operations.

`compile_rac_tie_lit_lod0_mesh_v1` accepts a parsed class, its qualified initial
palette, and source-ordered material IDs. It emits model-local neutral mesh
vertices and retains primitive, triangle and material submission order. Missing
indices are rejected. This helper explicitly compiles LOD0 at morph factor
zero. The general `compile_rac_tie_lit_mesh_v1` also accepts a source LOD/morph
selection and retains ordered MUL/ADD displacement and MULA/MADD color blending.
Every fixed or dinky position also uses source MUL from its signed integer base;
the convenient host-float parser positions are not reused. The first actual
candidate exposed a one-bit host rounding difference in two coordinates.
Neutral UV lowering explicitly requires the source Q lane to equal 4096.
Shrub and TIE wind mode bits remain meaningful and are not removed by computing
an initial palette.

## Camera-dependent TIE selection

`parse_rac_tie_lod_class_v1` assembles an explicitly selected table 0, 1 or 2
through the same bounded parser; it never substitutes another table when the
selected table is empty. The source class's thresholds at `+10/+14/+18`, mode
bits at `+24`, and selected-table identity remain explicit compiler metadata.

The loader computes the bounding center through `1f9ec0`, multiplies it by
class scale (`1f9c48`), then adds the authored instance translation (`1f9bd8`).
`23724c` subtracts the original camera position. `23726c/237274/23727c` multiply
the relative center by the original view columns in source ACC order. VMAX at
`2372dc` clamps center Z against zero; VMR32 at `237308` moves that result into
the live record's Y lane. That nonnegative view-space center depth is the LOD
metric. It is neither Euclidean distance nor the sphere's near extent.

`2375a0` computes threshold minus depth in three lanes. Raw sign-bit tests at
`237628/237630/237638` select: fixed LOD0 through threshold X; LOD0 morph
between X and Y; LOD1 morph between Y and Z; fixed LOD2 beyond Z. Equality at
Y belongs to LOD0 with t=1, and equality at Z belongs to LOD1 with t=1. The
source DIV computes t=(depth-lower)/(upper-lower); the source weights are
`{t,0,256-256*t,256*t}`, with separate ordered MUL and SUB. For t=1/3, for
example, they are `3eaaaaab/00000000/432aaaab/42aaaaaa`; a host expression or
exponent adjustment need not reproduce these words.

The actual scenic camera is authored in the first background scene record:
position bits `432ad38d/42e90ec0/42093704`, Euler bits
`3feec2b0/a5000000/3fe535ea`. The existing camera compiler qualifies the source
view columns. All camera records in all fifteen background chunks are constant.
Using these inputs, the source LOD gate produces 128 fixed-LOD0 candidates,
23 LOD0 morphs, 135 LOD1 morphs, and 10 fixed-LOD2 candidates. No selected
table is empty. Portal and frustum admission remain separate and these are
not visible-object counts. Class 2020 mode 2 and class 2390 mode 4 also require
the original per-instance basis deformation before placement.

The independent candidate fixture contains all 296 placements, 165,939 encoded
vertices and 83,659 fat vertices. It executes the original color and position
morph upper instructions and has SHA-256
`e269c1f5e0534e0a9152123ebd304ec852771b7c0867fc980b198c3af438cb94`.
The optional final input to the lighting probe compares every candidate's
center, depth, LOD, weights, and selected vertex position/RGBA against it.
The native comparison passes all 296 candidates, including 199,778 vertices
after duplicate GS-write expansion and 162,086 triangles. This validates local
geometry and LOD inputs; final source world/clip-matrix composition and visibility
remain separate qualifications.

## Shrub base geometry and per-pass state

`compile_rac_shrub_lit_base_mesh_v1` lowers parsed local geometry and its
qualified palette, retaining source primitive/triangle order. Original shrub
VU overlay 912339 maps LMA `1022e8/102af0/1032f8` to VMA `0/800/1000`.
ITOF0 at `2b8/2d0` converts signed positions; ITOF12 at `2c0/2d8` converts
signed ST/H. Every one of the 12,258 authored vertices has H=4096, which the
compiler explicitly requires. RGB loads at `440/488` use the source normal
index masked by `7fff`; alpha comes from palette entry zero's W loaded at
`378`, and is retained across the RGB-only loads.

The helper takes an explicit material ID for every primitive. All 719 frontend
primitive tags have PRIM `7c`: triangle strip, Gouraud, texture, fog and alpha
blend enabled, with STQ perspective coordinates. The source texture registers
and primitive tag must both be resolved by the material compiler. Raw source
TEX1 words (`40000ff92` and `40000ff72`) contain fields that need their original
loader relocation; they are not final sampler descriptors.
Shrub rendering also installs per-pass TEST state: resident `1dee00` contains
`5320b` (GEQUAL32, RGB_ONLY), whereas the common frontend restore contains
`5360b` (GEQUAL96, RGB_ONLY). Applying a single common threshold to every shrub
pass loses this distinction.

The mesh helper does not select full geometry versus the source billboard
path. Wind owner `22a608..22a784` modifies the instance's basis, not its local
vertices. Its output therefore belongs to source-qualified placement state at
the selected tick. All six authored wind classes retain that requirement;
compiling their shared base meshes does not freeze or complete their wind.
The shared wind distance gate reads resident parameters at `1e01f0`; its squared
distance threshold is 6000. Executing the original distance arithmetic for the
scenic camera leaves all authored wind candidates active: 2 TIE mode-2, 34 TIE
mode-4, 28 shrub mode-2, and 66 shrub mode-4 placements. There is no distance-gate
bypass that would justify static placement for these 130 candidates. TIE phase
hashes `(live_matrix_pointer>>9)&1ff`, while shrubs hash their live pointer
directly; both also consume the original frame counter. These live inputs have
not been replaced with guessed phases.

## Material relocation proof

The frontend calls TIE relocation `203f68` and shrub relocation `204340`.
The material loops `204138..204254` and `204700..204828` rebuild the final
registers from authored local slots and the original 16-byte texture descriptor.
For a raw TEX1 pair `{K,minimum}`, the final word is
`(K<<32) | (minimum<<6) | 0x20 | ((descriptor_mip_count-1)<<2)`.
For raw CLAMP words `{S,T}`, it is `S | (T<<2) | (global_texture_id<<24)`.
The observed S/T values are only zero or one, so the embedded texture ID lies
in region fields unused by their repeat/clamp modes.

Shrub local slot lookup actually reads material `+20` (the raw MIPTBP1 word).
All 379 source occurrences duplicate this value in raw TEX0 `+30`; preserving
only the latter happens to match this corpus, but the loader owns the former.
TIE slots are selected by the local ADGIF row ordinal.

The independent original-instruction diagnostic executes 57,777 EE instructions,
including the original PLZCW log2 helper, over 329 TIE and 379 shrub material
occurrences. Every final register agrees with the source construction. All final
TEX0 words select PSMT8, TCC=1 and TFX=MODULATE; TEX1 selects MMAG=1, MMIN=4
and three or four mip levels. K is signed 12-bit sixteenths: -110/16 for all TIE
and 376 shrub occurrences, -142/16 for the other three shrubs. The fixture
SHA-256 is
`253655a3ea2f6736ba69b88ee4c60f2628042739f8ea2c4ee7121c4414e5c301`.

This proves relocation and semantic input ownership. The original automatic
GS mip choice uses its source Q and K; a host derivative sampler with a bias
is not established as equivalent merely by decoding TEX1. Mip image recovery,
neutral source-Q sampling policy, per-pass TEST and fog remain renderer/compiler
integration requirements.

## Qualification evidence

The independent original EE wrapper/VU trace covers all 865 frontend
placements: 296 TIE and 569 shrub. It executes 639,278 EE steps and 396,660 VU
pairs. The native palette consumer skips each wrapper's final unused
speculative quartet and executes 358,600 pairs. All 32,600 output colors match,
including 4,673 distinct RGBA values. Concatenated palette SHA-256:
`f8568336044c3a3c4da81e8f89a707e0aacac5786e2d0aa3586444fdbc87d02d`.

The independent TIE stream scan covers all 48 classes, 810 high-detail packets,
17,556 dinky and 19,247 fat vertices, and 932 alignment bytes. All source Q lanes
are 4096. Its source ownership fixture contains 36,803 encoded vertices and
has SHA-256
`5d802e992f58ab29daee8fd8c23eac1975352ff56c54a3afa2729eeccceeafc7`.
Executing the original four fat-color arithmetic words for every C0 byte with
three C1/C2 extreme or patterned cases gives 768 matching LOD0 outputs.

These are source/reference comparisons with independently decoded original
instructions. They are not physical-console captures, cycle qualification,
complete LOD/wind behavior, or an end-to-end frontend completion claim.
