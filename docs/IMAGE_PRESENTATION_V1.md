# ImagePresentationV1

`openrc.image-presentation`, schema 1, presents an opaque display-encoded RGBA8
image after a finite sequence of feedback color transfers. Each transfer has
256 entries mapping a previous RGB channel byte to its next byte; alpha stays
opaque. Transfers operate on the preceding framebuffer, not on the new image.
Lead/tail update counts and an integer cadence preserve presentation ordering.

Initialization begins after the image is presented. Work elapsed while loading
the following neutral resources receives credit from an explicit quantized,
wrapping clock. Credit is divided by a positive divisor and subtracted from
the minimum update count; the runtime waits only the remaining updates. The
following scene cannot begin before its actual loading finishes.

These are neutral pixel and time data. WAD compression, source RGBA conventions,
TOC selection, register addresses and GS blend arithmetic stay in the compiler.

The 64-byte little-endian header is `ORIMGPR1`, u32 schema 1, u32 header size 64,
u64 total bytes, u32 transfer count, u32 zero flags, SHA-256 of the body. The
48-byte body prefix contains twelve u32 fields: width, height, aspect numerator,
aspect denominator, update rate, lead count, tail count, initialization clock
Hz, clock modulus, credit divisor, minimum updates and reserved zero. Transfer
tables precede tightly packed RGBA pixels. Count/byte partitions are checked
before allocation. Unknown flags, nonopaque pixels and invalid clocks fail.

The RAC compiler follows `1e9c7c..1ebd68`: unit-scale fade12, selected bitmap,
then frontend initialization changing time scale to `3f555555` before timer180.
The latter yields 150 source updates. `201af0` transfers 512x448 pixels to
the image buffer, then `1e9d14->1fb8a8` uses packet 151c60 to stretch that
image to the logical 512x512 display. The neutral image therefore retains
512x448 coded pixels with display aspect 1/1.
`1f4e08` uses an initial/final VSync and
one per fade iteration. The untextured source sprite and ALPHA44 give
`floor(Cd*(128-alpha)/128)`, with alpha from the original integer quotient.
This follows the signed GS blend equation in the
[original GS User's Manual, section 3.8](https://www.scribd.com/document/784545197/GS-Users-Manual).
The initialization clock records the nominal PAL HBlank rate, 16-bit wrap and
source divisor265. Its source mode83 enables the HBlank clock; see the
[timer register documentation](https://psi-rockin.github.io/ps2tek/#eetimers).
Wall-clock timing on a PC is not a measurement of original disc-loading time
or analog scanout phase.

The permanent tests cover byte/pixel preservation, feedback rounding, timing
credit boundaries and wrap, malformed input and the compiler's nested source
bitmap bounds. Runtime captures compare the actual D3D11 framebuffer with the
prepared image. This image stage does not imply a completed menu or boot I/O.

`FrameColorTransferSequenceV1` (`openrc.frame-color-transfers`) uses the same
256-entry byte transfer tables without storing an input bitmap. The caller
captures its previous completed framebuffer once, then applies each table to
the preceding result. Its independent lead and tail update counts surround
the per-presentation transfers. The codec bounds and hashes timing and every
table before allocation; there is no source blend register or runtime source
division. New Game uses source-compiled resources for its three fade durations.
