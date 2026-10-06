# Original title presentation

`rac_frontend_title` executes the title portion of original `1eb600..1eb760`,
extracts its original two image planes and emits its existing `1f5800` draw
packets. It is compiler-only. Source addresses, raw texture alpha and GS
commands are not runtime resource fields. It does not manufacture a complete
frontend or replace the separately recovered scenic actors and menu objects.

`1ebf14/1ebf1c/1ebf24` initialize logo alpha, prompt alpha and counter to zero.
Mode zero increments the counter before drawing. After timer30, logo alpha
increases by one per update, saturating at64. After timer120, prompt alpha is
`96 + TRUNC(32*cos(angle))`, where the source constructs angle from the signed
remainder `(counter-timer120)%60` using ordered COP1 operations. Both original
constants and the VU polynomial are retained. The source unit and PAL
time-scale encodings are explicit inputs. The existing COP1 timer reference
executes the PAL scale`3f555555`: timer30=25, timer120=100, timer60=50.

The cosine callee `1f9f90` invokes VU address`c80`. Startup `201fcc` calls
`2347f0` with source DMA chain`10e4c0`, uploading the corresponding VU0 code.
Its nineteen pairs`c80..d10` use `3fc90fda/40490fda/c0490fda`, while the COP1
caller uses negative-pi`c0490fdb`. Pair`c98` reads the previous positive-pi I
value while loading negative pi. The polynomial uses ordered multiplication
and explicit `MULA/MADDA/MADD` accumulator state, including the overflow latch,
through the existing integer numerical references. No host trigonometry or
reassociated arithmetic is used. This is source/reference qualification;
it is not a new physical-console capture.

The input gate runs after this update, even before the images first appear.
Pressed bits`0x840` reach the existing `219e60` adapter and request the real
menu initializer. Mode three resets the title counter to timer60 and subtracts
16 from both alphas, clamping at zero. The separate `21a1a0` owner performs
menu initialization and lifecycle; this module does not execute it implicitly.

The title draws are:

| Image | Original source | Rectangle in source render raster | UV extent |
| --- | --- | --- | --- |
| Logo | WAD shared base + header84; direct RGBA32 upload`1eb0d0` | x236,y16,256x128 | 256x128 |
| Prompt | Texture catalog selector`max(language-1,0)+4`; `1f4868` | x160,y(height-80),192x96 | 256x128 |

The prompt is an authored localized texture, not generated text. The catalog
is the already decoded WAD header58/5c/68 catalog: `1eb044..1eb068` binds it
through `203038`. The six earlier TOC reads`1500/1508/1510/1518/1520/14f8`
are consumed by separate initialization (`2032d0` and related owners); they
are not necessary image resources for these two title draws.

`emit_rac_frontend_title_v1` retains draw order, live TEX0, live Y/X offsets,
height, original source alpha and complete packets. Source`1f5800` subtracts
one half-pixel from XY corners; at scale1:1 this puts pixel centers on texel
centers. The prompt's scale4/3 in texture coordinates requires the original
DDA/filter precision. Source render raster512x448 and final logical display
512x512 remain distinct; see [the scene compiler](RAC_FRONTEND_SCENE_COMPILE_V1.md).

## Pixel semantics and reference qualification

Immediately before the title draws, `1fb848` submits packet151a00. Its builder
`1fb10c..1fb128` establishes TEX1_1=`0x100000261`: base-level bilinear filtering
for minification and magnification. Then`1f3c10` submits literal13d0c0,
establishing ALPHA_1=`0x8000000044`, TEST_1=`0x5360b`, CLAMP_1=0 and COLCLAMP=1.
The title TEX0 uses RGBA MODULATE. All RGB fragment inputs are128, so only
alpha changes after filtering. The alpha test is GEQUAL96; failed fragments
still write RGB through RGB_ONLY, with alpha/depth writes disabled.

The [Sony GS manual, sections3.4.8/3.4.9 and3.8](https://github.com/ninjadynamics/PS2Docs/blob/main/GS_Users_Manual.pdf)
defines filtering before modulation and modulation as the product shifted
right by seven. The resulting source alpha must remain a numerator over128
for blending. Logo texels have alpha values above128, so clamping source
alpha during initial asset conversion loses real information. The extraction
API preserves all original bits, including CLUT reordering for the prompt.

`compile_rac_frontend_title_logo_overlay_v1` lowers the independently qualified
logo layer to the existing neutral overlay format: timer30 invisible updates,
64 one-step alpha variants and a held final frame (89 frames for PAL). Its sample positions land
exactly on texel centers, with exact unit DDA gradients, so no fractional
filter rounding is required. Every coverage byte is the original source alpha
times the current vertex alpha, shifted right by seven. This is explicitly
the logo layer, not a completed title or a replacement for the localized prompt.

`rasterize_rac_frontend_title_prompt_v1` now lowers the original prompt through
an explicit bounded public-reference model. Original 12.4 triangle corners
and UVs define an affine plane. At local integer pixel coordinate n, the
coordinate after the texel-center offset is `(n+1/2)*4/3-1/2`; its four-bit
fixed-point representation is therefore `floor((64*n+8)/3)`. Both original
triangles give the same value. Production evaluation uses unsigned rational
integer arithmetic, with no host floating-point interpolation or epsilon.
The permanent and original-source probes independently derive these
coordinates using triangle signed-area barycentric weights.

Each coordinate has four fractional bits. Both horizontal rows are separately
interpolated and floored, followed by a separately floored vertical
interpolation. This ordered filter follows the published
[PCSX2 software sampling reference](https://github.com/PCSX2/pcsx2/blob/37a8be7b42d22960c82043ad4e394944df7b085e/pcsx2/GS/Renderers/SW/GSDrawScanline.cpp)
and its `lerp16_4` numerical operation in
[GSVector4i](https://github.com/PCSX2/pcsx2/blob/37a8be7b42d22960c82043ad4e394944df7b085e/pcsx2/GS/GSVector4i.h).
Each wrapped neighbor is selected before filtering. RGBA MODULATE follows
the filter; vertex RGB128 leaves RGB unchanged, while coverage is
`floor(filtered_alpha*vertex_alpha/128)`. Every reached localized CLUT alpha
is at most128; the compiler rejects source planes outside that domain.

The affine coordinate qualification is deliberately separate from physical
GS DDA qualification. Public implementations differ here: GSdx uses rounded
floating-point triangle setup and truncated 16.16 SIMD stepping, while
[paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs/tree/3a66c1976170cbc2cb53a3593fabbc7c4b2ccfbd)
uses affine interpolation and four-bit floor snapping with an epsilon.
Its original author's
[research](https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation/)
and README explicitly avoid claiming hardware bit accuracy for varying
interpolation. This compiler's exact rational coordinates implement the
documented affine/four-bit model; they do not claim byte equality to either
entire emulator, to their architecture-dependent stepping, or to an unmeasured
physical DDA. Filtering and modulation have their separately stated ordered
reference above. This boundary is numerical qualification, not replacement
artwork or a missing prompt. No third-party implementation is incorporated.

`compile_rac_frontend_title_overlay_v1` emits the complete original idle title
to `ScreenOverlayV1`: 64 logo variants and 128 prompt-alpha variants. The latter
also include values reached by the original mode-three fade. PAL has160
frames: the source timer120 prefix is100 updates, followed by the60-frame
prompt suffix, with `loop_begin=100`. First visible prompt uses phase1, and
the final suffix frame uses phase0. Source draw order is logo then prompt.
The prompt rectangle remains192x96 at (160,height-80), so source viewport
clipping removes its final16 rows before the separate display blit.

The neutral contract contains sampled RGB and coverage over128. Filtering
and modulation have already happened; runtime consumes neither original
UV/GS packets nor original texture alpha. Standard alpha/255 blending would
change this integer result. Early Start and the separately recovered real
menu transition remain controlled by their original callbacks, independently
of the looping title presentation.

## Local evidence

Original data and output stay in ignored`local/forensics`:

- Boot ELF SHA-256
  `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`.
- Frontend decoded WAD SHA-256
  `159e6341c3180831940350097bbbbf2115b9d35e4a030a856d563206b462c2d2`.
- VU overlay32, file offset`f450`, LMA`10e4d0`, VMA`c80`, size`2f0`.
- Logo source range`42a340..44a340`, SHA-256
  `dc2d1224426b5422659aa355a9c43070029451681c96fd2d0b698f43318b7f95`.
- Prompt entries4..8 have a shared1024-byte CLUT and separate32768-byte
  PSMT8 planes. Language0 and1 select the same original prompt.
- `rac_frontend_title_source_probe.cpp` executes nineteen decoded original
  pairs for all sixty source phases with two unrelated I/ACC/register states.
  It exports original planes to`frontend-transition/title`, without source
  data in tracked tests. It also checks all six localized source selections
  against the independent triangle/filter oracle at three alpha values
  (1327104 pixel components), and exports the complete `title.orscreen`.
  All executable builds/audits/runs are coordinated
  through root CMake and verified`build-portable` targets.

The permanent tests use synthetic source planes and check initialization,
thresholds, periodic phases, early Start, mode-three fade, word wrap, original
packet positions/ordering, language selection and retention of source alpha.
The prompt raster tests compare442368 synthetic pixel components over both
original triangles and six alpha values; complete title codec, loop boundary,
draw order and unsupported source raster/alpha domains are also checked.

Coordinated CMake/PE-audited execution passed all four permanent groups and
the original-source comparisons described above. The complete original PAL
title overlay has192 images,160 frames,loop100 and17830404 encoded bytes;
SHA256`cf39d80131c24c96e4a03566d90c73001c6a3cf5dbf9e3cc2a254809bbd0e8b2`.
