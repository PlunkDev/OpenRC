# Original frontend scene compilation

`compile_rac_frontend_scene_v1` lowers the original global frontend WAD into
the existing `ActorLibraryV1` and `ActorAnimationBankV1`. It derives the complete
actor collection from the source scene directory and preserves source ordinal
identity, including repeated classes. It adds no runtime source decoder or new
mesh, texture, skeleton or animation format.

The original asset owner `1eabe8` obtains the frontend WAD from TOC `+14e8`,
uploads its GS images through `203958`, and binds shared class/texture data
through `203e78`. The compiler validates the contiguous GS source/destination
layout before passing that image span to the existing texture decoder. Class
rows retain their original local texture-slot mappings. All three source class
directories and shared section starts bound the individual Moby class assets.
Missing/repeated identities, invalid texture sentinels, unsupported GS layouts,
changed actor identities between chunks and insufficient aggregate output
limits are rejected.

The existing Moby parser, complete bind-pose compiler, actor baker and scene
animation compiler recover the actual geometry/material/texture/rig/pose data.
No special-material fallback policy is supplied. The five scenic source classes
have no metal packets. The separate class1138 menu objects are owned by their
original menu controller and are not appended to the scenic collection.

On 2026-09-12 the CMake-built, PE-audited portable source probe passed with:

| Output | Verified count |
| --- | ---: |
| Scenic classes, in actor order | 0, 530, 1635, 1905, 1564 |
| Actors / chunks / clips | 5 / 15 / 75 |
| Compiled frames / joints | 3575 / 169 |
| Vertices / triangles / texture bindings | 10921 / 11444 / 8 |
| Sampled poses over one complete source loop | 6990 |
| Posed vertices checked for finite coordinates | 15267558 |
| Encoded actor / animation bytes | 1465475 / 7860143 |

There are nine source Moby textures; the ninth binding belongs to the separate
menu-object class. The eight above are the textures used by all five scenic
actors. The probe samples the original 1398-update clock, including every chunk
reload and the final return to chunk zero. This proves neutral asset preparation
and existing player/skinning execution; it does not assert bit-exact general
skeletal interpolation or completion of the frontend renderer/controller.

The source loader `204fc0` creates each actor through `20d348 -> 20d440`.
`20d45c` clears the entire object. `20d568..20d574` copies class scale to
object `+2c` and initializes animation speed/rate to one. The loader leaves
rotation at zero. Update `1eb5b4` writes interpolated root XYZ at object `+10`
and calls `20ed48` to update the object matrix. Class scale is already folded
into the neutral geometry/poses by the existing baker and must not be applied
again as a neutral entity scale. The source camera uses the separate
`1eb338 -> 125358/1254a0/125548/1253f8` path and projection owner `1f3140`;
the gameplay camera is not an alternative frontend camera.

`compile_rac_frontend_timeline_v1` lowers the complete owner loop into existing
`SceneTimelineV1`, with the separately qualified source camera as an explicit
input. It checks every complete source camera record for constancy, checks
position/horizontal tangent against the owner input, resolves neutral actor
bindings by semantic key, pins hashes of the two actual encoded payloads and
validates every sampled clip/frame/phase binding. Ordinary sampling begins at
source update1 and the final loop sample reloads source update0.

For PAL, the render buffer and final display raster differ. Setup `1f38ec`
supplies source height448, while `1f38fc` supplies display height512.
`1fab40` stores these at draw environment `+152` and `+15a` respectively.
The compositor's display dimensions must be **512 by 512**, independently of
the projection's source viewport512 by448. `1fb8a8` submits packet151c60,
generated at `1fae50..1faf08`: source UV512 by448 maps to destination XYZ512
by512. This applies to the post-intro bitmap as well: `201af0` uploads its
512 by448 pixels at TRXPOS0 into the source buffer, then `1e9d14` invokes the
same display blit. It is stretched to the original logical display raster;
there is no authored extra64-row black padding. Analog display aspect remains
a separate issue.

## Bounded menu joint extractor

`sample_rac_frontend_menu_joint_translations_v1` executes the qualified
`211808` joint-extraction path used by `20db98`, for the reached class1138
menu frames. It returns source joint units for all five joints, before the
consumer applies class scale, object rotation and position. The four consumer
inputs 0,1,2,3 are selectors: `211548` resolves model `+1c` metadata and uses
the last joint in each selector's path. Actual class1138 paths are `[0,2]`,
`[0,1]`, `[0,4]`, `[0,3]`, so corners use joints **2,1,4,3**. The source unit
conversion occurs **after** hierarchy composition in `20db98`: class scale
times `1/1024`, then `1f9c30`, `1f9ec0`, `1f9bd8`. Reordering that conversion
into each local joint translation can change the final rounded coordinates.

The admitted domain is explicitly checked: five joints in a star hierarchy,
root quaternion `(0,0,-32768,0)`, four identity-axis quaternions
`(0,0,0,32767)`, complete signed-i16 sparse translations, no local scale,
and adjacent or identical frames from one sequence at phase 0, 0.5 or 1,
plus the original stopped `last -> 0` endpoints at phase 0 or 1.
Changing rotations, cross-sequence transitions, arbitrary phases and different
hierarchies require their original numerical paths and are rejected here.

For this domain, source `21193c..211964` selects the adjacent-frame branch
`211c0c -> 211d34`, which skips quaternion normalization. The constant root
rotation is exactly `diag(-1,-1,+1)`; children are identity. Nonnegative
scale-tag zero is skipped by `211df8` / `211b28`, so terminal root scale does
not alter these joint translations. The actual `VMULA/VMADD` interpolation
at `211be8..211bec` and hierarchy chain `21222c..212238` have only exact
i16/half-integer, zero and unit operands. Every intermediate magnitude is at
most 65536. Integer half-unit evaluation therefore retains the actual results
without approximating a general VU accumulator or replacing an unknown MADD
with separately rounded operations. Neutral zero is canonicalized to +0.

The stopped forward/reverse animation keeps `last -> 0` frame indices.
Phase zero takes the direct source branch; phase one takes quaternion
normalization. The root's squared length is exactly one and `VRSQRT(1,1)=1`.
Children have only quaternion W nonzero, so their matrices remain identity
regardless of rounding in normalized W. This proves the same translations
at these exact endpoints without claiming general quaternion equivalence.

Synthetic tests cover complete actor/texture/animation composition, repeated
class identities, bounded failures and this extractor's signed-i16 extrema,
terminal-scale behavior and rejection boundaries. Source probe mode
`<frontend-class1138.bin> --menu-poses` additionally checks the reached slots
`0,1,16,17,18,5..13` and reports any rounding difference from the general
neutral TRS player separately from source-path qualification.

## Local provenance

Copyrighted input and generated artifacts remain under ignored `local/`:

- `local/forensics/frontend-transition/frontend.wad.decoded.bin`, 7545280 B,
  SHA-256 `159e6341c3180831940350097bbbbf2115b9d35e4a030a856d563206b462c2d2`.
- Resident `SCES_509.16`, SHA-256
  `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`;
  correct R5900 disassembly: `local/forensics/boot_r5900.disasm.txt`.
- Source assets: `local/forensics/frontend-transition/frontend-class*.bin`
  and `background-00.scene.bin` through `background-14.scene.bin`.
- Neutral probe output: `local/forensics/frontend-transition/neutral/`
  (`frontend-actors.oractor`, `frontend-animation.oranim`).
- Probe implementation: `local/forensics/rac_frontend_scene_source_probe.cpp`;
  CMake target `openrc-rac-frontend-scene-source-probe`.
- Detailed original owner, copyright prelude and transition evidence:
  `local/forensics/rac-frontend-transition-evidence.md`.

Only synthetic source data is used in tracked tests. Source identities and
decoded chunks in the compiler result support lowering the original clock
into a neutral timeline; they must not cross into runtime resources.

The timeline camera binding distinguishes the original projection input
`3f2147ae` from effective neutral inverse-projection tangents. The ordered
`1f3140` matrix construction and compiler inverse adaptation produce
horizontal `3f2147af` and vertical `3ef3daf9`, one ULP above their original
inputs. The compiler validates both the source input and these effective
values; it does not substitute the literal input for the executed matrix.

## Static environment compilation

`decode_rac_frontend_environment_assets_v1` retains the original frontend
terrain, TIE, shrub, gameplay and sky ownership independently from the five
animated actors. The source has156 terrain records,48 TIE classes/296 placements,
33 shrub classes/569 placements and51/122/69 respective texture bindings.
Sky ownership runs from shared+header14 to shared+header60; header78 points
inside sky pixels and cannot bound that resource. Raw gameplay bytes remain
compiler-side so lighting consumers can recover their original environment.

`compile_rac_frontend_terrain_v1` executes every entry16 record through the
resident55907 program with an explicit identity diagnostic transform, and
requires normal E termination and complete GS packets. It follows the exact
VIF index→descriptor→source-position chain and checks every recovered color
against its GS vertex. All156 records now complete, producing10962 referenced
vertices and9036 triangles with all51 textures. This recovers the source full
detail geometry; the diagnostic transform is not the frontend render camera.
Entry16's source byte450 applies a camera-dependent coarse-point morph before
emission. Its actual scenic-camera result is not yet qualified by this
diagnostic full-detail extraction. The v10 static environment therefore
remains partial; fixed-camera source55907 execution, including live fog
constants and any resulting coarse-point interpolation, is separate work.

The source emitter prefetches vertices beyond the end of a strip. In record4,
pair4a3 (byte2518) reads through an indeterminate descriptor after the last live
index. The shared executor now conservatively joins all possible RAM values
for a partly known read address. It retains only bits proven common to every
candidate, and continues when the value is dead. Unknown stores, packet bases
and control still stop. Synthetic tests compare all five load families against
their concrete address alternatives, including wrapping, masked lanes,
partially known RAM and live versus dead unknown results.

Terrain binding204918 creates RGBA MODULATE TEX0 (204ac4..204ad8) and repeat
addressing (204b78). The compiler uses raw palette alpha, encoded integer color
and modulation denominator128. After sky,1e9e90..1e9e9c restores TEST1=5360b:
alpha GEQUAL96, failed alpha writes RGB only, and GEQUAL source depth. The
terrain GIFtag at1deaf0 has PRIM7c, including alpha blending;22c53c..22c548
restores ALPHA=8000000044, source-over with denominator128. The neutral
`alpha_failure=rgb_only` policy retains failed-fragment color while preserving
destination alpha and depth. It occupies material byte1e in both static and
actor codecs; byte1f stays reserved and all legacy defaults remain wire-zero.

`compile_rac_frontend_sky_shells_v1` preserves the four authored shells in
source order, with90 clusters,1465 vertices,1472 triangles and4 textures.
Positions are camera-relative; source alpha remains0..128, UV interpolation is
affine, and depth projection is to the far plane. It does not include the256
procedural sprites drawn between shells1 and2 by22c188. Source fog and the
procedural sprite owner remain separate qualification/integration work;
geometry/material preparation alone does not establish completed frontend
rendering or a physical GS raster match.
