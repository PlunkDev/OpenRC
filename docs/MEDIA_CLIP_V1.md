# Prepared original startup media

`MediaClipV1` carries standard MPEG-2 elementary video packets, their original
90 kHz PTS/DTS, and interleaved signed PCM16 audio. Decode order is retained;
timestamps are not sorted to conceal B-picture reordering. The initial Windows
adapter presents limited-range BT.601 video, matching the recovered startup
sequence's BT.470BG matrix. It is not a general colour-managed video player.

The asset compiler removes MPEG program-stream/PES framing and Sony private
audio framing. The observed `ff a1 00 00` payloads form one continuous
`SShd`/`SSbd` stream. Codec 16 audio is deinterleaved in header-specified blocks
and decoded with the existing PS ADPCM decoder, retaining predictor history
across both blocks and PES packets. Source audio codecs stay compiler-side.

For the supplied PAL build, boot initialization reads the region byte at disc
sector 289, byte 0x33. The actual `P` selects TOC entry 0x1800, not entry 0x17f8.
The selected MPEG contains 363 pictures at 25 Hz, coded as 512x416 pixels, and
640,976 stereo sample frames at 44.1 kHz. The compiler records the original
512x512 logical movie raster as aspect 1:1. Analogue display stretch and
physical overscan have not been measured. Neither coded-pixel aspect nor a
guessed 4:3 rectangle replaces that recovered raster contract.

The existing `PreparedGameV2` manifest optionally references a shared
`LevelPackageV1` outside its planet list. The v8 native compiler stores the
selected clip as `startup/intro`, type `openrc.media-clip`, schema 1, in
`shared.orlevel`. Its provenance binds the source image, boot executable,
selected movie extent and compiler pass. There remain nineteen level packages,
each with eight resources. An exact, fully verified v7 installation can retain
its level bytes during the transactional v8 upgrade; older numerical compiler
profiles cannot use this path.

Prepare reuse reconstructs the small canonical shared package from the selected
ISO movie and compares its complete size and digest before accepting reuse.
This binds the source TOC range/hash and prepared media, even if a modified
package has internally consistent container hashes. Offline profile validation
checks structure/provenance consistency; it cannot authenticate source bytes
without the ISO. A source mismatch preserves the existing installation.

## Payload encoding

All fields are little-endian. The 64-byte header contains `ORMEDIA1`, version 1,
header size 64, total byte size, video-packet count, reserved zero and SHA-256
of the body. The body contains eight u32 fields (coded width/height, frame-rate
numerator/denominator, display-aspect numerator/denominator, audio rate/channels),
an i64 audio PTS and u64 scalar audio-sample count. Each video packet contains
i64 PTS, i64 DTS, u32 byte size, reserved zero and the elementary bytes. PCM16
samples follow all packets. Timestamp -1 means absent. Limits are checked
before allocation and decoding; trailing bytes and unknown flags are rejected.

## Execution and present boundary

The same Windows runtime loads the shared package when `--level` is absent.
It feeds elementary packets to a synchronous Media Foundation decoder, handles
output-format changes, and drains delayed pictures after the final input.
The existing D3D11 renderer presents the decoded frames. A per-playback waveOut
owner sends prepared PCM to the audio device; its sample position drives the
video clock. Completion requires both final-picture duration and audio drain.
The source intro does not accept controller skip. Closing the application
does not count as successful playback.

`--prepared-root <root> --smoke-test` exercises the entire prepared intro.
Optional `--smoke-capture <absolute.ppm>` reads back an actual D3D11 frame during
that playback. It also compares 961 interior samples to the displayed decoder
frame with a three-byte-value tolerance; the PAL run has maximum error 1/255.
The pass-through pixel shader preserves already display-encoded RGB, avoiding
the scene shader's extra linear-to-sRGB conversion. Explicit `--level` retains
the existing developer gameplay path.
Normal launcher Play selects startup. The2026-10-03 v14 runtime checkpoint
continues through copyright, the original frontend, live menu/no-card dialog
and confirmed New Game through actual scene/audio retirement and preparation,
then all three original movies, three loading cards and seven fades. The normal
sequence smoke confirms616/1348/415 movie frames and completed Veldin loading
and cleanup, followed by actual level-state installation in the same session,
with Oculus selected only for the test process. The next missing consumer is
`level/enter`; `level_state_installed=1` does not imply entity admission.
`level_playable=0` remains explicit; full gameplay is not established by this
successful presentation path. The separate audio-retirement diagnostic retains
its narrower component scope.

The codec adapter follows Microsoft's [MPEG-2 decoder interface](https://learn.microsoft.com/en-us/windows/win32/medfound/mpeg-2-video-decoder)
and [stream-change protocol](https://learn.microsoft.com/en-us/windows/win32/medfound/handling-stream-changes).
FFprobe was used only as an independent local picture-count check, not as a
runtime dependency. Original clips, traces and captured frames remain under
ignored `local/forensics/startup-media`.

The decoder adapter exposes packet availability and actual decoder drain as
separate states. `stop_input_and_drain` stops accepting prepared packets;
subsequent `next_frame` calls still return pending decoder output. It does not
claim audio or final-frame presentation completion. The first original New
Game clip produces616 frames normally; stopping after100 returns12 buffered
frames before actual drain. Both cases were verified with the system decoder.

The renderer's submission drain uses a real D3D11 event and polls its result.
A superseded event token is rejected. Normal intro cleanup retires audio and
decoder ownership and awaits GPU completion before the next presentation.
This follows Microsoft's [event-query contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query),
and does not substitute for New Game's other presentation and loading owners.

`AudioClipV1` is the separate finite PCM-only neutral resource
`openrc.audio-clip`. Its codec stores an explicit sample rate, one or two
interleaved channels and signed little-endian16-bit samples. Counts, complete
channel frames, payload size, reserved fields and SHA-256 are checked before
sample allocation. It introduces no video image, source bank or ADPCM decoder.
The existing Windows PCM device accepts either this resource or a movie's
audio span, with the same actual start, sample clock and checked retirement.
Clip storage must outlive the device owner.

The runtime now uses one `run_prepared_movie` operation for movie presentation.
`--smoke-stage media-library` explicitly diagnoses the three admitted New Game
movies and an interrupted movie on the same renderer. It does not run loading
cards, acknowledge frontend consumers, or establish the normal New Game flow.
The actual test presented616/1348/415 frames, then112 frames for a stop requested
after100; audio owners retired and GPU events completed for each playback.
The source stop can retire a queue before its natural completion, which remains
separately visible in the diagnostic result.
