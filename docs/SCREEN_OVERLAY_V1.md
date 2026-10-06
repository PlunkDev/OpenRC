# ScreenOverlayV1

Neutral prepared screen pixels, drawn in order over the existing D3D11 scene
or media presentation. The compiler owns texture decoding, filtering and
modulation; runtime receives display-encoded RGB and integer coverage only.

Each image is an interleaved RGB/coverage byte plane. Coverage is a numerator
over the resource's explicit denominator (1..255), bounded by that denominator.
For each RGB component the compositor evaluates
`destination + floor((source-destination)*coverage/denominator)`.
The equivalent positive weighted sum permits exact unsigned integer division
in the pixel shader. Output alpha is opaque. This is neither normalized
alpha/255 nor linear-light blending.

Draws select an image and signed integer canvas coordinates, with one image
pixel per canvas pixel. Rectangles clip before addressing; order is significant.
Images are prepared at the logical source raster. D3D presentation maps that
raster to the existing media/scene display viewport, using point selection of
prepared pixels. This contract does not certify the compiler's source sampler,
the 3D rasterizer, analogue output, or physical console accuracy.

An independent integer-Hz clock selects frames. `loop_begin=UINT32_MAX` holds
the final frame; any valid frame index specifies a looping suffix after a
one-way prefix. Images may be shared across frames; no source code or source
memory is carried across the package boundary.

The little-endian codec has a 64-byte header with eight signature bytes
`ORSCROV1`, schema1, header size64, total byte count u64, image count u32,
frame count u32 and SHA-256 of all following bytes. Its 32-byte prefix contains
canvas width/height, updates/second, denominator, loop begin and three zero
words. Each image has width/height followed by exactly width*height*4 bytes.
Each frame has a draw count, then `(image_id,u32; x,i32; y,i32)` records.
There are no unknown flags, trailing bytes or external paths.

Default limits: 64 MiB, dimension4096, 1024 images, 100000 frames and 1000000
aggregate draws. Admission validates byte/record bounds before allocation,
cross references, clock, dimensions and every coverage byte.

The renderer may admit an ordered bank through `set_screen_overlay_layers`.
Each layer retains its canvas, coverage denominator and frame list; its images
are uploaded once. `set_screen_overlay_layer_frames` supplies one optional
frame index per layer, with an absent index hiding that layer. Selection changes
do not upload textures, and invalid selections leave all layers unchanged.
The GPU draws each layer and each frame's draws in their supplied order, so
overlap retains every preceding integer rounding step. The legacy single-overlay
setter replaces the whole bank and `clear_screen_overlay` retires every layer.

Layer admission accepts explicit `ScreenOverlayLimitsV1` and applies byte,
image, frame and draw limits to the entire bank. A larger dialog bank can raise
the image count explicitly while retaining its chosen aggregate byte budget.
Failed validation or upload leaves the previous bank available.

Verification: codec/cadence/negative clipping/invalid references and all
8,454,144 combinations of source/destination byte and denominator128 coverage
pass independent weighted-sum checks. The explicit Windows target
`openrc-screen-overlay-d3d-tests` compares 1,179,648 GPU pixels to CPU, including
nine coverage values, both darkening and brightening, negative clipping,
ordered overlap and clearing the overlay. A further 720,896 GPU pixels match
ordered three-layer composition with different denominators, independent frames,
hidden/empty layers, and reversed layer order. Tests also verify atomic rejection
of invalid selections and aggregate limits, explicit admission of 1,025 images
under a raised limit, and compatibility with legacy replacement/clear calls.
It uses a hidden diagnostic window
and is intentionally outside unattended CTest. Build and PE audit it through
CMake before launching its executable under `build-portable`.
