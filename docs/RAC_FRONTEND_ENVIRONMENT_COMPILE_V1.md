# Frontend environment compiler

`rac_frontend_environment_compile` is compiler-side relocation and lowering of
the original frontend WAD. Runtime resources contain neutral geometry and
images only. The original camera and scenic actors were already correctly
projected; their empty surroundings were caused by missing environment assets.

The qualified resident is SCES-50916, ELF SHA-256
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`.
The ignored local WAD is
`local/forensics/frontend-transition/frontend.wad.decoded.bin`, 7,545,280 bytes,
SHA-256 `159e6341c3180831940350097bbbbf2115b9d35e4a030a856d563206b462c2d2`.
Original bytes and probe outputs are not repository assets.

## Original ownership and placements

`1eabe8` loads the frontend WAD through TOC `14e8`. Its shared base is header
`+04`, here `5c000`. Terrain is shared + header `+10`; `1eaf30` passes it to
`204918`. Sky is shared + header `+14`; `1eaf48` passes it to `203118`.
The TIE directory is header `+20/+24`, stride 32; shrub is `+28/+2c`, stride 48.
Both directories retain original class bindings and material-slot mappings.

The gameplay owner is shared + header `+7c`, installed by `1eb130 -> 1e9ec8`.
Its required environment blocks are TIE classes/instances at `+30/+34` and
shrub classes/instances at `+38/+3c`. The frontend intentionally omits unrelated
gameplay blocks. `parse_rac_gameplay_environment_v1` uses the same strict class
and instance readers as the complete-level reader, with boundaries formed only
from present, validated source pointers. It does not invent omitted blocks.

The original data has 156 terrain records, 48 TIE classes / 296 placements and
33 shrub classes / 569 placements. Texture banks contain 51, 122 and 69 images.
The placement matrices are authored source data: TIE records are `e0` bytes,
shrub records `70` bytes, each after its 16-byte instance-list header.

## Four sky shells

The complete sky owner is WAD `[c0640,110c40)`, 329,216 bytes. Header `+78`
falls inside its texels and is not an independently installed owner in
`1eabe8`; it must not truncate the sky graph. The next real owner is shared +
header `+60`, the general texture pixel store.

`compile_rac_frontend_sky_shells_v1` lowers the four authored shells in source
order, with 90 clusters, 1,465 vertices and 1,472 triangles. `22c188` invokes
shells 0 and 1, then the separate procedural-sprite owner, then shells 2 and 3.
The static helper deliberately reports only its four shells. The 246 fixed
stars and 10 moving sprites still require the original seed/update path and insertion between
shells 1 and 2 before the complete sky can be claimed.

`22c188` initializes the shared shell matrix to identity; `22d2ac` projects
signed 16-bit XYZ without subtracting camera position. Neutral positions are
XYZ / 1024, instances are camera-relative, and depth is projected to the far
plane. The source emission discards projected Z and writes GS depth zero.

`22cc40 -> 22d3f8` reads an authored RGBA32 gradient from the cluster's
attribute array. This is not a white or diagnostic material. The textured
owner `22ca00 -> 22d520` emits RGB `(128,128,128)` and the original vertex
alpha byte. `22d5d0` zero-extends the two 16-bit ST values before VITOF12;
`22d634..63c` sets Q to exactly 1. UV and Gouraud color interpolation are
affine in screen space. Shells use source textures 4..7; images 0..3 belong
to the separate sprite owner. The original PSMT8 CLUT permutation is applied,
and alpha stays in the original 0..128 range.

Gouraud RGBA always follows screen-space affine interpolation independently
of UV. The GS User's Manual v6.0 p35 describes RGBA/Fog DDA gradients; p60
section3.4.10 applies perspective division only to interpolated S/T/Q. The
Supplement p6 section1.1 explicitly describes RGB interpolation on triangle
edges and scanlines. Local primary references are the ignored
`local/forensics/GS_Users_Manual.txt` and `GS_Users_Manual_Supplement.txt`.
Neutral encoded materials interpolate decoded byte colors, with their
perspective/affine selection applying to texture coordinates only.

Resident packet `13d1f0` establishes the gradient shell's ZBUF `31000000`,
TEST `30000`, ALPHA `8000000044`, PRIM `4b`. Packet `13d260` establishes
textured-shell ZBUF `131000000`, TEST `3180b`, ALPHA `2000000044`, PRIM `5b`.
Both use CLAMP 5 and TEX1 `ff9000000260`. This means clamp-to-edge, bilinear
sampling, always-pass depth; shell 0 writes depth and the others do not.
Textured alpha-test failure is FB_ONLY and depth writes are masked, so the
test does not suppress shell RGB. Both blend encoded RGB using
`Cd + floor((Cs-Cd)*As/128)`. FIX differs but is not selected by either ALPHA
equation. Standard alpha/255 blending is not an equivalent contract.

The neutral material extension expresses those operations without serializing
GS registers. The existing renderer owns rasterization. The original
subpixel edge/GS timing model is not established merely by this geometry
lowering.

## Terrain and source fog

Terrain executes resident VU program 55907 and requires normal E termination
for every record before it lowers pre-projection XYZ/STQ/RGBA. All 156 records
now complete, yielding 10,962 vertices and 9,036 triangles with 51 textures.
Record 4 originally stopped at pair4a3 on a dead, one-past-index descriptor
prefetch. The shared VU executor now joins every possible RAM candidate for
an indeterminate load address, preserving only bits common to all candidates.
Unknown stores, XGKICK and live control remain strict; no record is skipped.

These counts qualify the full-detail endpoint under the explicit diagnostic
identity transform. Original entry16 calls the depth-dependent coarse-point
morph owner at VU byte450 before emission. The current v10 static geometry
has not qualified that morph under the actual scenic camera. The existing
frontend timeline compiler proves every32-byte camera record is identical
across all15 source chunks, so the original camera/morph/fog may be compiled
once after matching a live-context55907 trace. An initial-XYZ host depth dot
product is insufficient evidence. The v10 captured environment remains
partial and must not be described as original-complete geometry.

The terrain binder204918 sets RGBA MODULATE and repeat addressing. Raw CLUT
alpha is preserved; modulation uses denominator128. After sky,1e9e90 restores
TEST5360b: alpha GEQUAL96, failed alpha writes RGB only, source GEQUAL depth.
Seed1deaf0 has PRIM7c (including ABE and FGE);22c53c restores source-over ALPHA
8000000044. Neutral encoded color and `alpha_failure=rgb_only` preserve those
material operations. Materials whose every texel and referenced vertex have
alpha128 are lowered to opaque after proving the alpha test always passes and
source-over reduces exactly to source RGB. The original source proves this
for 44 materials / 8,578 triangles; 7 materials / 458 triangles retain blending.
The existing D3D source capture compared optimized and fully blended scenes
at1280x720: byte-identical PPM SHA-256
`fbe965818bcc975492b65407942b2617a43b085c91466574f0ad7b4e4a2bab7d`.
That proof uses decoded-byte, affine color interpolation; interpolating UNORM
colors or perspective colors first can incorrectly turn constant alpha128
into127 and is not an equivalent encoded material policy.

`compile_rac_frontend_fog_v1` follows1e9ec8 ->1f2930 ->1f3140. The frontend
gameplay header+0 points to settings+b0, WAD6945b0. RGB fields+0c/+10/+14
are read by LBU, yielding FOGCOL(47,26,15). Near/far raw distances are0 and
240640 (235 world units); factor endpoints are255 and53.55. Ordered COP1
operations produce the original projection scale/intercept and world gradient.
`evaluate_rac_frontend_terrain_fog_v1` consumes original transformed W, adds
the source offset, clamps to the two factors, and extracts bits4..11 after
FTOI4. Tests cover exact dyadic endpoints, nonzero near distance, fractional
truncation and invalid bounds. This qualifies compiler operations, not a fog
implementation in the neutral renderer; fog must follow texture modulation.
The original inputs yield world-gradient bits `bf5b73b1`, projection-scale
`bcdb73b1`, projection-W depth coefficient `ba5b73b0` and offset `437f0000`.
At world depths 0, 1, 50, 100, 200, 235 and 300, the sampled emitted fog bytes
are 255, 254, 212, 169, 83, 53 and 53. The coefficient's final one-ULP
difference follows the separate source COP1 multiply and is retained.

## Remaining source work

Compiler-only sky-sprite state helpers now execute the original256-entry
initialization/update owner. They retain source LCG state, COP1 angle/size
arithmetic and actual VU0 c80/c90 cosine/sine microprograms. Initializing
resets RNG to12345, generates246 fixed centers and10 moving centers, and
immediately runs the first color/phase update. The exposed shared RNG word
allows the caller to account for intervening original consumers; isolated
owner stepping does not assert a complete frontend clock. The original
22ceb8 / VU1 overlay221571 billboard emission and neutral animation resource
remain to be integrated. A static list of centers is not the final sky.
The source probe passed initialization and60 owner updates: every sine/cosine
microprogram returned normally with known output despite unknown unrelated
VU state; all246 fixed centers stayed fixed and all10 moving phases advanced
with nonnegative reflected height. RNG states `fdddbe27` after update1 and
`da13514d` after update60 independently match the source call counts
`246*8+10*2+246` and another `59*246` calls, respectively.

The billboard owner is22ceb8 with VU1 overlay221571, entries0 and20. Its
visibility coefficients come from1f3140 through the original1fa058 atan
polynomial (resident vectors1de6a0/1de6b0 and quadrant table1de6c0), followed
by the originalc80 cosine. It tests the sign of
`tangent * viewZ - (abs(viewXY) - size * frustumCoefficient)` separately
for X and Y. At source update1,230 of256 sprites pass this test;252 centers
have positive depth. All230 execute the original program through normal E,
producing920 known XYZF/STQ/ADC vertices and460 emitted strip triangles.
All16 possible signs of the four caller lanes cleared by multiplication
with0 produce identical packet words. The source tangent bits are
`3f2147ae/3ef3daf8` and frustum coefficients `3f9748ac/3f8dc6cc`.

The logical packet qualification retains a source scheduling detail:
XGKICK at1e8 precedes the E-delay ISW at1f0 that patches the last tag's EOP.
The diagnostic reads the final RAM patch while retaining the original kick
address/order. This is not qualification of PATH1 timing; the common executor
keeps its synchronous snapshot semantics. Dynamic texture TBP/CBP address
bits remain unknown, while source32x32 PSMT8 dimensions and format are known.
The packets confirm PRIM54, ALPHA48 (additive RGB with denominator128),
ST pairs(0,0),(1,0),(0,1),(1,1), all Q=1. This additive material still needs
a neutral renderer contract and an integrated source animation clock.

`sample_rac_frontend_sky_billboards_v1` now lowers these ordered source
operations into compiler-only GS XY16/Z/F and RGBA values. Its direct
arithmetic comparison against the complete original VU packets passed at
source-owner updates1,60,1398,4096:230,230,229,230 visible sprites and
920,920,916,920 exact vertices respectively. Each comparison includes all16
caller zero-sign cases, both source Q values, RGBA, XY16, Z and F. The CMake
portable import audit and environment tests passed. The source-owner clock
still excludes other global RNG consumers, and this compiler-only output
does not yet change prepared resources.

The pending actual-camera terrain pass also needs the live coarse-point
coefficients, beyond the camera matrix. Binder204918 reads terrain header+8
(`41400000`,12 in this source) and writes three distances multiplied by6,4,2:
72,48,24 world units. Owner234380 combines these with18d020, the live fog
world gradient, and rewrites1deb70..1deba0. Entry16's byte450 path consumes
these values at VU qwords667/669 and blends both position and color. The
current identity-camera recovery does not certify that camera-dependent
blend; these source-input owners identify the remaining compilation work.

The earlier TIE baker's white diagnostic vertex color is not an authored
lighting result. TIE `238688` computes a 64-entry lit palette from the class
normal table and each placement's RGB1555 palette. Shrub `22b8f8` computes a
24-entry lit palette from class `+2c` normals and placement RGB. Both use the
resident light tables and their actual normalized instance basis. Shrub
entries are VCALLMS 0/160; TIE entries are 2c0/420. Shader state, lighting and
per-vertex palette binding must be closed before those previews are admitted
as prepared environment geometry.

Shrub class flags also reach wind deformation: IDs 466, 502, 503, 504 and 510
have `+14 = 4`; ID 586 has `+14 = 2`. A static bake of all 569 shrub instances
would omit this source behavior.
