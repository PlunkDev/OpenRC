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
├── neutral SceneBlockDirectoryV1 parsing with owned blocks and 8-section layouts
├── bounded zero-copy SceneBlock VIF command parsing across sections 0-4
├── CompanionTerminalWadIndexV1 validation of the shared terminal WAD bank
├── bounded 2FIP indexed-texture parsing and RGBA/TGA conversion
├── neutral seven-region boundary-table parsing
├── MapArtV1 record inventory and three-panel TGA preview composition
├── bounded PS2D icon/save-template bundle parsing
├── SBlkBundleV3 parsing, PS2 ADPCM bank inventory, and per-reference tuning
├── clean-room linear PS ADPCM frame decoding
├── strict VAGp V1 parsing and bounded mono PCM16 WAV encoding
├── SHA-256 and prepared-game manifest
├── ELF32/MIPS executable and IOP/IRX module/import inventory
├── application directories
└── launcher settings

openrc-cli
├── disc inspection and inventory
├── TOC, WAD/bundle/companion, scene-block/VIF, 2FIP, MapArt, PS2D, VAGp, and SBlk diagnostics
├── prepared-game extraction
└── ELF and IOP/IRX inspection and diagnostics

openrc-launcher
├── image selection
├── disc inspection
├── asynchronous Prepare/Cancel and progress
├── data-directory access
└── future runtime entry point
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
