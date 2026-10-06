# Original frontend decoration v1

The main menu has two separate rendering paths. `21a6cc` calls
`20e180(object,1)`, which submits that one ordinary moby to `212658`. The
existing class1138 model, rig and sequence representation therefore belongs
to the normal actor/model pipeline. `2127a8..2127c4` reads the menu camera,
view and projection-view matrices, and `2127c8` scales the camera by 1024.
This source routing does not by itself qualify all model shading or GS state.

The separate model camera is `execute_rac_frontend_menu_camera_v1`:
position (256,256,64), right -Y, up +Z, forward +X. Its effective tangent
words are `3f2147af` and `3ef3daf9`; near/far are 0.03125/728 in neutral world
units. The global PAL scene display remains 512x512, while its source render
buffer is 512x448. This is the same projection adaptation used by
`execute_rac_frontend_menu_projection_v1`, not the animated background camera.
See [SOURCE_DIV_ACC_REFERENCE_V1.md](SOURCE_DIV_ACC_REFERENCE_V1.md).

After both RTT passes and the intervening `20e200` submissions, `21ac34`
opens a GS batch and `21ac60..21ac98` walks the original 14 slots in order.
It skips disabled slot flags and conditionally slot6 using root+0xd8. Every
selected slot calls `2250b8` with its actual object pointer. Null object/PVar
pointers return without draws or random-number consumption.

`execute_rac_frontend_decoration_v1` executes this subordinate `2250b8`
control and preserves four possible bitmap layers in source order. These
screen-space layers are additional to the actor mesh and the text RTT.

| Layer | Source selection | Source draw | Color | UV rectangle |
| --- | --- | --- | --- | --- |
| Base | sprite bank key `e99e`, variant7, via200198 | 2008b8 at225170 | `807f7f7f` | Complete selected source image |
| Active flash | texture catalog26 via1f4868 | 1f5800 at225218 | `00808080` | Two original RNG coordinates, followed by projected width/height |
| Overlay | texture catalog28 via1f4868 | 1f5800 at2252b8 | `50606060` | (0,0,width, arithmetic-shift(3*height,1)) |
| Border | texture catalog25 via1f4868 | 1f5800 at22531c | `80808080` | (1,1,62,62) |

Positions come from actual projected PVar fields +50/+54/+58/+5c. They are
not inferred from text labels or texture dimensions. The border adds one to
X/Y and adjusts each dimension by -2 below76, -1 below151, otherwise0.
Both source quad paths place XY on the original half-pixel boundary. The
sprite path receives source fixed coordinates `pixel_word << 4`; it is not
silently interchangeable with an already rasterized catalog quad.

The source culled region is x>=512, signed(x+width)<0, y>=449 for PAL
(417 otherwise), or signed(y+height)<0. It intentionally allows PAL y=448.
Adds, shifts, signed comparisons and wrap precede any neutral safety bounds.
The compiler result records the actual calls and their assets; binding,
decoding, residency and rasterization of those assets remain real downstream
work. A draw plan is not an emitted full menu screen.

Base, overlay and border retain ALPHA `8000000044`: source-alpha over
destination, with the FIX field unused because C selects source alpha. The
flash uses `(FIX << 32) | 68`, additive `Cs*FIX/128 + Cd`. Neither may be
replaced by the enclosing text RTT's different composite blend.

Flash state lives at PVar+48/+4c. An active flash first increments its age by
two, then consumes two range200 random draws. Its triangular intensity is
`min(128,2*(128-abs(age-128)))` over the reached0..256 cycle. It emits the last
zero-intensity draw at age256 before clearing the flag. Inactive flashes
consume one range2000 draw and begin a new age-zero cycle only on zero; that
call does not draw the flash yet.

The actual shared generator `1160d8` updates the source word by
`seed = seed * 41c64e6d + 3039` modulo32. `2140b0` selects
`((seed >> 16) & 7fff) % bound`. Its pointer is at12f86c, initially12f580;
the ELF-initialized word at that object's +58 is1. This is an initial state,
not proof that the seed is still1 on menu admission: other original random
consumers must retain their order. The helper takes and returns the shared
seed explicitly and never resets it per object or per frame.

The ignored fixture executes the real2250b8,2140b0,1160d8 and1f9b70
instructions in512 cases, producing1,148 draw calls and522 generator calls
over66,718 instructions. Resource lookup and draw entry points are explicit
ABI boundaries. Comparison covers control, source call order/selection,
arguments, ALPHA and state. It is not a GS raster or physical-console capture.
The source ELF SHA-256 is
`17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b`;
fixtures and original data remain ignored under `local/forensics`.
