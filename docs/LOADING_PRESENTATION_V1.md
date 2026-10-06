# Loading presentation v1

`LoadingPresentationV1` is a neutral prepared image library with periodic
background draw lists, labels, display positions, reveal times and integer
fade durations. Its nested `ScreenOverlayV1` contains already filtered RGB and
coverage. No source texture, palette, STQ, executable address or disc reader
is present. The outer and nested envelopes both validate complete payload
digests and bounded records before decoding variable data.

For an actual presented frame and current duration,
`materialize_loading_presentation_v1` produces a small ordinary ScreenOverlay.
The initial background opacity is min(denominator, frame*denominator/fade_in).
During the final fade it becomes (duration-frame)*denominator/fade_out. A
band's optional reveal ramp takes precedence while that ramp is active.
Each already filtered background coverage byte is multiplied by this opacity
and divided by the denominator with integer floor. Labels retain their fixed
coverage and are submitted after their corresponding backgrounds.

This separates image materialization from the actual sequence clock. If a
real pending load extends duration, the next materialized frame uses that new
duration. The host still performs actual display and I/O work before advancing
`FrontendSequencePlayerV1`; producing pixels alone acknowledges neither.
An individual materialized frame copies only its reached small images, not
the entire periodic library.

# Compiler and source domain

`compile_rac_loading_presentation_v1` uses the existing original loading draw
owner and qualified raster to prepare all600 periodic background updates.
It forces full background opacity before export, retaining the exact filtered
coverage. Later neutral opacity multiplication therefore performs the same
final MODULATE/128 operation as the source. It does not move alpha before
filtering. Every512x64 raster is independently checked for an exact128-pixel
horizontal period in all four components before storing a128x64 image and
four repeat draws. Equal rasters are deduplicated only after both SHA-256 and
full byte equality checks.

The two authored card images and positions come from actual executed draw
requests at updates0 and65. Pair0/1 and3/4 have two bands at y178 and224;
pair2/2 has one at y192. The qualified display is512x448 with source origin224.
Its common fade-in is32 updates and fade-out16. The second band begins at65
and overrides the common fade with its64..96 reveal ramp. RGB/coverage use
the same public-reference bilinear contract documented in
`RAC_FRONTEND_LOADING_V1.md`; physical GS DDA rounding remains unqualified.

`compile_rac_new_game_resources_v1(ISO, ELF bytes, language, selector, cadence)`
checks the supported ELF and global TOC digests, reads the selected loading
WAD with existing bounded readers, then emits three loading-presentation and
three media-clip `LevelPackageResourceV1` resources. Media uses the existing
PSS compiler's selected audio channel, original video decode order and PCM.
The result includes exact sequence ID/type/payload-hash bindings and source
ELF, TOC, WAD and three-movie hashes. No intermediate extracted files or
runtime source readers are needed.

Limits cover source ELF/WAD/movie extents, all three aggregate movie extents,
per-resource neutral decoding and aggregate output. Movie extents must not
overlap. Each loading raster library has a preflight bound below21MiB before
deduplication; all six payloads together are bounded at256MiB by default.

The permanent resource test compares complete512x448 neutral images against
the existing source draw/filter/modulation path across fade, reveal, repeated
period and extended-duration boundaries. Its optional two arguments are the
source ISO and ELF paths; that mode compiles all six actual resources and
roundtrips their neutral payloads. This tests preparation, not MFT decoding,
audio presentation, loader barriers or gameplay admission.

Runtime integration requires registration/decoding of
`openrc.loading-presentation` and `openrc.media-clip`, real loading/media
presentation consumers, and the concrete cleanup/level consumers enumerated
in `FRONTEND_SEQUENCE_V1.md`. Returning prepared resources does not close
those still-required operations.
