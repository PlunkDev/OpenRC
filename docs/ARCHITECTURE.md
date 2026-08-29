# OpenRC architecture

## Principles

1. The repository contains only original OpenRC source, tests, and metadata.
2. The user's disc image is read locally and is never modified.
3. Platform-independent code lives in `openrc_core`.
4. Reverse-engineering tools and the eventual runtime share one description of
   disc builds, symbols, executable overlays, and asset formats.
5. PCSX2 may be used as a behavioral reference during development, but it is not
   part of the target runtime.

## Current components

```text
openrc_core
├── bounded ISO 9660 reader and streamed file access
├── SYSTEM.CNF parser
├── disc/build identification
├── DiscTocV1 extent, local asset-table, and primary-container inventory
├── bounded WadV1 inspection and clean-room LZ decoding
├── decoded WadBundleV1 record inventory
├── streaming decoded-WAD corpus inventory, provenance, deduplication, and strict probes
├── deterministic structural candidate-family profiling and Veldin-ranked TSV export
├── strict bounded SceneAnimationBankV1 camera/actor/frame/root parsing
├── shared PAL five-language subtitle-directory timing/text parsing
├── neutral SceneBlockDirectoryV1 parsing with owned blocks and 8-section layouts
├── bounded zero-copy SceneBlock VIF command parsing across sections 0-4
├── conservative SceneBlock VIF execution with VU1 memory/write provenance
├── neutral SceneBlock VIF/VU phases with inherited state and exact qword runs
├── exact SceneBlock task preamble/record execution with dual-bank frame input
├── stateful XGKICK GIF/GS decoding and primitive/raster-state assembly
├── entry-16 source XYZ recovery with VIF/VU provenance and GS-color validation
├── profile-bound reusable ISO/ELF/SceneBlock runtime loading and execution
├── CompanionTerminalWadIndexV1 validation of the shared terminal WAD bank
├── bounded 2FIP indexed-texture parsing and RGBA/TGA conversion
├── neutral seven-region boundary-table parsing
├── MapArtV1 record inventory and three-panel TGA preview composition
├── bounded PS2D icon/save-template bundle parsing
├── SBlkBundleV3 parsing, PS2 ADPCM bank inventory, and per-reference tuning
├── clean-room linear PS ADPCM frame decoding
├── strict VAGp V1 parsing and bounded mono PCM16 WAV encoding
├── explicit-policy bounded SBlk mono PCM16 WAV encoding
├── SHA-256 and prepared-game manifest
├── ELF32/MIPS executable, typed DVP overlay, and IOP/IRX module/import inventory
├── bounded EE/R5900 code-region, control-transfer, and syscall-wrapper inventory
├── bounded neutral DVP VU decoding with typed control-flow/access inventory
├── application directories
└── launcher settings

openrc-cli
├── disc inspection and inventory
├── TOC, WAD/bundle/companion/corpus/family/scene-animation/subtitle diagnostics
├── scene-block/VIF/VU/GS, 2FIP, MapArt, PS2D, VAGp, and SBlk diagnostics
├── explicit-policy SBlk WAV and EE/R5900 boundary diagnostics
├── auto-fit wireframe TGA export from a selected real SceneBlock invocation
├── prepared-game extraction
└── ELF, DVP overlay/VU microprogram, and IOP/IRX inspection and diagnostics

openrc-launcher
├── image selection
├── disc inspection
├── asynchronous Prepare/Cancel and progress
├── data-directory access
└── verified adjacent-runtime process launch

openrc-runtime
├── named ISO/ELF/level/record/entry process contract
├── exact ISO-to-prepared-ELF SHA-256 binding before scene access
├── emitted GS-triangle conversion with raster-context coordinates
├── recovered-source 3D wireframe with bounded debug orbit controls
├── Tab comparison against the decoded GS 2D output
└── native D3D11 submission, auto-fit resize, and WARP fallback
```

## Planned components

```text
tools/
├── audio, texture, model, and remaining TOC-table decoders
├── MIPS/R5900 analysis pipeline
├── symbol and type database
└── asset converters

runtime/
├── platform and input
├── renderer
├── audio
├── game memory model
├── reconstructed game logic
└── save system
```

## Native-code strategy decision

The project will not choose between source decompilation and static
recompilation blindly. After extracting the selected retail build, Stage 1 will
inventory the main executable, level overlays, relocations, imports, and VU
programs. A representative set of functions will then be reconstructed to
measure compiler-pattern recovery and correctness.

The long-term runtime may combine:

- manually reconstructed C++ for engine and gameplay systems;
- generated native code for well-understood R5900 functions;
- native replacements for PS2 kernel, GS, VU, IOP, audio, input, and file I/O
  interactions.

The final program must not require a PS2 BIOS or execute through a general PS2
emulator.

## DVP VU analysis boundary

The neutral DVP VU decoder accepts a bounded ELF byte span and overlay metadata,
composes non-overlapping code chunks in the VU1 microaddress space, and splits
each little-endian eight-byte pair into the lower word at offset zero and the
upper word at offset four. It always preserves both raw words. The result owns
only metadata and source ranges and retains no input pointers. The recognized
subset covers the upper and lower encodings required by confirmed SceneBlock
overlay group `55907`; an unrecognized half remains explicit rather than being
assigned a guessed instruction.

Decoded metadata includes upper I/E/M/D/T flags, typed operands, VU1-wrapped
direct branch targets, indirect transfers, and typed VU1 data-memory, XTOP,
and XGKICK accesses. A derived control-flow inventory exposes those
relationships independently of execution.

The bounded-functional VU1 executor consumes only decoded program metadata and
an owned explicit state. Individual 32-bit values carry a known-bit mask, so
missing VIF/runtime inputs propagate as indeterminate rather than guessed
zeroes. Upper and lower halves read the same pre-pair snapshot; direct and
indirect control flow executes exactly one delay pair, E executes one final
pair, and the confirmed four-pair STATUS/CLIP and seven-pair Q visibility are
modeled. Stores are queued, while a diagnostic XGKICK mode commits older
stores and copies a bounded, VU-RAM-wrapped GIFtag sequence through EOP.

This is not yet a cycle- or bit-exact VU1. General FMAC/load scoreboarding, the
VU multiplier's exact rounding, and live PATH1 arbitration remain future
layers. Exact SceneBlock task execution now includes its constant seed,
four-qword frame transform in both input banks, entry-specific DMA/VIF packet,
entry-0 poststate, and carried BASE/OFFSET/DBF plus VIF/VU state. Complete
XGKICK events feed a bounded stateful GIF/GS decoder which assembles typed
vertices and primitive emissions. A profile-bound owning loader now shares
that exact ISO/ELF verification and execution path with the first native D3D11
viewer. For entry 16, a profile-bound recovery layer identifies the one V4-8
UNPACK whose output count matches the decoded GS submission count, then
requires its complete index-to-descriptor-W-to-signed-XYZ/adjacent-RGBA chain
to agree with every decoded GS color. The selected index address is therefore
derived per record instead of frozen to record 0's qword 250, and every hop
retains its last VIF write. Full-level mode independently starts every record
from the same validated entry-0/TOP=0 state, merges complete raster and source
triangle batches under aggregate limits, and reports diagnostic GS-only records
separately. The viewer can orbit the merged source mesh, but that orbit is
isolated PC-side diagnostic state: it never rewrites the VU frame transform or
claims to reproduce the original camera. Scene classification, textures, the
live game camera, and gameplay remain outside this layer.

The current CFG is deliberately context-insensitive and folds each decoded
delay pair into its transfer block. It rejects an instruction-run start,
entrypoint, direct target, or another control transfer in a decoded delay slot,
as well as multiple control effects in one instruction pair. The decoder
returns an explicit error for those shapes instead of publishing a graph that
could bypass required delay-slot execution. A direct call has one call edge;
its decoded continuation address remains metadata rather than a speculative
return edge.

## Disc data model

The reference image exposes only three ordinary ISO 9660 files: `SYSTEM.CNF`,
the boot ELF, and the IOP module image. Most of the image is therefore not
described by ordinary directory records. OpenRC keeps the verified ISO file
inventory separate from the raw-sector data layer. The current `DiscTocV1`
reader validates the global table and the 19 per-level descriptors; further
Stage 1 work has now confirmed the WadV1 wrapper and its bounded LZ stream.
Decoded buffers remain separate from the source image and are accepted only
when every input read, match distance, alignment marker, output limit, and
record boundary validates. The first decoded bundle is split into exact
aligned records and classified as nested WAD or ELF without assigning
unproven gameplay meanings to those records. Confirmed 2FIP payloads are
decoded into owned indexed images; palette lookup normalizes the PS2 PSMT8
CLUT permutation before platform-neutral RGBA conversion.
The corpus inventory walks each decoded WAD observation one at a time, retains
only owned provenance/hash/classification metadata, and probes only the first
occurrence of each decoded SHA-256. Nested provenance identifies both the
deduplicated parent payload and the exact parent observation. Format probes use
separate byte and scene-record envelopes; a parser's dedicated format rejection
can mean `no_match`, while allocation failures, foreign exceptions, and outer
inventory-envelope failures propagate instead of becoming an `unknown`
classification.
Every local resource-block WAD run is now parsed as one complete
`SceneAnimationBankV1`. The parser validates either observed header tag, the
aligned actor-offset table, camera cadence, shared scene/frame metadata,
monotonic frame tables, exact declared frame sizes, and one 16-byte root
transform per frame. Large camera and animation bodies remain borrowed ranges;
only bounded metadata is owned. An optional PAL localization tail uses the
shared subtitle-directory parser, while empty entries, empty strings, and
opaque bytes after the logical text envelope remain representable rather than
being reinterpreted.
MapArtV1 composes three palette-compatible images without assigning gameplay
meaning to the remaining opaque regions. The PS2D parser likewise retains
unknown header words and tagged payload keys rather than treating guesses as
format contracts.

Prepared files are written into a unique staging directory, hashed while they
are streamed, described by a deterministic manifest, and published with a
no-replace directory rename. Existing preparations are rehashed before reuse.

## Configuration and generated data

On Windows:

- `%APPDATA%\PlunkDev\OpenRC` contains portable user preferences;
- `%LOCALAPPDATA%\PlunkDev\OpenRC` contains extracted data and machine-specific
  state;
- cache and logs are stored in `cache` and `logs` below the local directory.

The selected ISO path is stored in
`%LOCALAPPDATA%\PlunkDev\OpenRC\launcher.ini`. On first load, an older
roaming `launcher.ini` is copied there automatically and retained as a backup.
Game data must never be written to the roaming configuration directory.
