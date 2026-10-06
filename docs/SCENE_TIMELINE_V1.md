# SceneTimelineV1

`openrc.scene-timeline`, schema 1, supplies explicit neutral camera, actor
transform and animation selections on an integer update clock. The first
sample is the first presentation update; a looping sequence returns to sample
zero after its last sample. Nonlooping sequences hold their final sample and
leave the following transition to their owner.

The resource pins the SHA-256 of the complete actor-library and animation-bank
payloads. Actor bindings contain local dense model/rig indices and an affine
model-to-entity transform. Each sample contains a camera and exactly one
clip/frame/phase, world transform and visibility record for each binding.
Admission checks matching model/rig keys, rig digests, frame bounds, joint
counts and interpolation guards before the renderer receives the sequence.

Camera position, right/up/forward and horizontal/vertical half-angle tangents
are explicit. Near/far planes and display aspect are independent; the renderer
does not infer a gameplay camera, normalize an authored basis or change a
frustum to fit a window. It uses a centered viewport with the supplied display
aspect. Actors use the existing neutral skeleton player and world-actor GPU
resources without a player proxy or synthetic level.

The little-endian format has a 64-byte header: `ORSCTIM1`, u32 schema 1,
u32 header size 64, u64 total bytes, u32 actor count, u32 sample count, and
SHA-256 of all following bytes. The 96-byte body prefix contains the two
payload digests, update rate, display aspect numerator/denominator, loop flag,
and four f32 clear-color values. Each actor binding is 56 bytes (two indices
and twelve affine components). Each sample contains sixteen f32 camera
components followed by one 56-byte actor state per binding: clip, frame,
f32 phase, enabled flag, XYZ position, XYZW quaternion and XYZ scale.

Decoding checks the exact byte partition before allocation, all flags, hashes,
finite component bounds and aggregate sample limits. Camera bases must be
nonsingular and quaternions unit length; singular authored actor scales remain
valid for the existing position-only skinning path.

`scene-timeline` tests cover canonical round trips, clock wrap/hold, malformed
counts, payload integrity, camera/transform rejection and cross-resource pose
admission. A codec or actor submission test alone is not original-flow evidence.
