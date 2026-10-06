# Original frontend objects and animation

This compiler-side owner recovers the actual class1138 menu objects. It does
not define a replacement menu or add source-format state to the runtime.
The caller lowers the resulting geometry and control into the existing neutral
package boundary.

## Admission and update order

`219e90` admits fourteen objects through the loop at `219fe8`:
`226720(1138) → 20d348 → 20d440`, followed by a direct `20ed48` post, packed
metadata `20e340`, and the owner writes at `219ffc..21a07c`. The earlier
`226d50(1)` loop creates five render-target descriptors; those are distinct
from the fourteen objects.

`admit_rac_frontend_objects_v1` reuses the existing allocator, fresh constructor,
sequence setter and executed zero-rotation post adapter. It preserves the real
allocator cursor, guard slot, current count and cleared128-byte PVar blocks in
the canonical staged session state. All fourteen admissions commit together;
insufficient source capacity or an unresolved owner leaves the caller state
unchanged. Resident allocator globals receive additional alias exclusions.

The first post runs at position zero and sequence zero. Only afterward does
the owner copy the camera XYZ, install callback `23b578`, and select each
screen sequence's last frame. Initial scale comes from the actual model.
Registered callback and class-table index are explicit loader bindings; neither
is guessed from class ID.

An additional local fixture runs the actual resident constructor `20d440`,
its `20d6d0` leaf and the real `1f99b0` word-clear loop on class1138. It contains
28 explicit loader/live-index cases and11,298 instructions, with every byte of
each256-byte object recorded. It uses the existing
`openrc-moby-fresh-source-compare` input format. Parent rebuilt/audited the
target and confirmed all28 full256-byte source/native comparisons. SHA-256:
`895049483913382f21744af2d94040129dd0193b3d72de1195d94e83eb30075f`.

The reached class1138 domain has zero XYZ rotation, no spatial or auxiliary
owner, model flags zero, and ordinary sequences below255. The original update
list at `213c78` executes animation pre `20e3d0`, callback `23b578`, then post
`20ed48`. The separate calls preserve this order: the callback geometry uses
the matrix produced by the **preceding** post.

## Sequence clock and stopping

`rac_frontend_object_animation` decodes actual ordinary Moby sequence headers,
frame references and rates. The source class has219 sequences: the first14
contain one static frame with rate zero; the rest contain seven frames at
rate0.5. Reached sequences have sound byte255 and no triggers.

The executed pre/stop contract admits phase0,0.5,1 and speed0,+1,-1, with
homogeneous rate0 or0.5. Its integer half-unit arithmetic proves every reached
COP1 product, sum and division exactly; it is not a host approximation to a
general hidden ACC. Setter `213d28/20d6d0` preserves phase and speed, clamps the
selected frame, and performs the same ordered writes as the source.

Forward wrapping sets previous/current to last/zero, phase zero and speed zero.
Reverse wrapping leaves last/zero, phase one and speed zero. The callback stops
only when the pre-step raises flags bit2. Static zero-rate sequences keep the
source speed and only receive the original flags write.

The original finite source runner produced8,541 setter/pre/stop cases and
35,109 writes from492,484 original instructions. Direct native comparison and
three permanent groups passed through the audited CMake target. The local
fixture SHA-256 is
`2743079bc855de29a5521aa099718f22620bff5f20db622986f9072d8e71a01b`.
The script, copyrighted input and fixture remain under ignored `local`.

## Screen transition stage

`step_rac_frontend_object_screen_v1` executes `21a2f4..21a500`: the signed
12-update gate, request/current/previous screen fields, actual forward/reverse
sequence selection for all fourteen objects, ordered node+14 bindings, and
source sound request arguments. Initial `219e90` sets current=requested=main,
so the first update begins a real same-screen reverse transition.

The main screen is `1d4948`; its sequence slots are
`0,1,16,17,18,5,6,7,8,9,10,11,12,13`. Its first two slots share the same empty
node `1ce678`. The following three nodes are `1d4a18`, `1d4a68`, `1d4ab8`.
Their reached cleanup/init callbacks are zero. A reached nonzero node lifecycle
callback is rejected until its own implementation is integrated. Input, card
bookkeeping and camera setup remain separate owners.

The source stage tests only `mode==1` for the countdown arm. Every other raw
mode uses the request/idle arm. In particular main screen+3c is46 and slot
screen+3c is31; initial mode2 is not the active-screen mode. The first native
fixture run caught an erroneous1/2-only gate; it was removed without changing
the source fixture, and permanent cases now retain the actual46/31 values.

The local original-instruction fixture contains18 stage cases and10 ordered
node writes, SHA-256
`eb49f53e674e98c94f9356d678f4b08b0fa0484cd8500820327b74c5f59fe0f6`.
These cases include initial transition, arrival, idle, main-to-slot start,
and signed zero/negative countdown gates. It does not pretend to execute the
slot screen's nonzero arrival initializers.

## Executed corner geometry

`sample_rac_frontend_object_corners_v1` composes the qualified source-unit joint
sampler from `RAC_FRONTEND_SCENE_COMPILE_V1.md` with the real `20db98` consumer.
Sequence and frame references must agree with the decoded class owner.

The words at `160fd0` are selectors0,1,2,3. `211548` resolves them through
`model+1c`, the four path pointers and the final byte of each two-joint path.
For the actual source they select joints **2,1,4,3**. They are not direct
joint indices0,1,2,3. The decoder validates bounded paths and reads their tails;
it does not hardcode this output map.

All five translation qwords have W=1: `212168` initializes VF19 from VF0,
`212204` changes XYZ only, and the hierarchy chain `21222c..212238` combines
basis W=0 with translation W=1. The consumer then performs, in source order:

1. COP1 MUL of object scale by raw`3a800000` (1/1024).
2. `1f9c30` VU MUL of joint XYZ by that result, preserving W.
3. `1f9ec0` transform through the preceding object post matrix, with the
   explicit source MULA/MADDA/MADDA/MADD accumulator chain.
4. `1f9bd8` VU ADD of object position to XYZ, preserving W.

These are raw-word integer numeric references, with their documented reference
qualification retained. They are not a physical-console capture or proof of
unmodeled latency/status effects. General neutral host pose matrices are not
used as expected source coordinates: scale-before-hierarchy creates measurable
rounding differences on the actual class.

The local corner fixture executes original `20db98` and its numeric callees,
with independently checked fixed-quaternion/i16 joint-entry algebra and the
independent integer multiplier/adder reference. It covers204 cases (the102
source pose states at two positions),35,292 original instructions and2,448
corner qword stores. Fixture SHA-256:
`cd7b182e46350d12343c7cc0cd4485a3ddc586373a30221134e18df4d114e171`.
It is explicitly a source-consumer/reference comparison, not an unqualified
full raw execution of `211808`.

The object test target accepts optional screen and corner fixture paths in
that order. Parent's CMake build and PE audit passed, then all18 screen cases,
10 node writes,204 corner cases and3,264 corner words matched. Its three
permanent groups passed. The geometry cases exercise selector resolution,
half-phase source coordinates, previous-matrix/position ordering and malformed
metadata/ownership rejection. These native comparisons retain the qualified
source-entry and arithmetic-reference boundary described above.

## Remaining boundary

This corner callable returns the four completed qwords at PVar+00,+10,+20,+30.
The later `23b5d0` edge differences and `1f9cb8` VSQRT lengths at+40/+44 are
outside it. No host square root, guessed Q result, or zero length is installed
as a completed source result. Completing these owners and integrating the
actual menu draw/input/runtime flow remain separate work.
