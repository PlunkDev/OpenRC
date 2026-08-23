# OpenRC

OpenRC is an early-stage native reimplementation project for **Ratchet & Clank
(2002)**. The long-term goal is to run the game on modern computers without a
PlayStation 2 emulator while preserving the original gameplay and enabling
modern platform features.

> [!IMPORTANT]
> OpenRC does not contain game code, game assets, disc images, Sony SDK files,
> or leaked material. You must provide your own legally obtained PlayStation 2
> disc image. Local game data is excluded from version control.

## Current status

Stage 1 is in progress. The repository currently provides:

- a C++20 core library;
- a bounded ISO 9660 reader, `SYSTEM.CNF` parser, and exact boot-file lookup;
- a validated `DiscTocV1` inventory for the hidden global and per-level
  sector ranges used by the reference build;
- validated local TOC tables for VAG audio, WAD runs, primary subranges, and
  opaque per-level record tables;
- a bounded `WadV1` reader, clean-room LZ decoder, and validated inventory of
  the first decoded bundle containing nested WAD and ELF records;
- a bounded `2FIP` indexed-texture parser with PS2 CLUT normalization,
  RGBA expansion, and lossless TGA export;
- a neutral seven-region boundary-table parser for decoded level payloads;
- a bounded `MapArtV1` parser that exposes 256 region-0 records and composes
  the three 128x128 2FIP layers into a native TGA preview;
- a bounded PS2D save-bundle parser for `icon.sys`, the memory-card icon model,
  and the tagged save-template records;
- a bounded `SBlkBundleV3` parser plus a strict per-level PS2 ADPCM bank
  inventory with deduplicated sample ranges, owned item metadata, and signed
  per-reference center-note/fine tuning;
- a bounded clean-room PS ADPCM frame decoder with explicit control-flag
  metadata and no assumed sample rate or playback policy;
- a strict VAGp V1 parser with owned metadata, decoded content ranges, and
  bounded mono PCM16 WAV export using the rate stored in each asset;
- a neutral `SceneBlockDirectoryV1` parser for the large decoded per-level
  container, with exact chained block envelopes and owned trailing data;
- a bounded `CompanionTerminalWadIndexV1` parser that validates the 21 aligned
  terminal WadV1 records shared by every reference level;
- streamed SHA-256 inventory and extraction into an immutable prepared-game
  directory with a deterministic manifest;
- ELF32/MIPS span/path parsing with program/section inventory plus bounded
  IOP/IRX module, relocation, and import metadata;
- recognition of the PAL (`SCES-50916`) reference executable and detection of
  the NTSC-U/C (`SCUS-97199`) release;
- a native Windows launcher with disc inspection, asynchronous Prepare,
  progress, and cancellation support that remembers the selected image;
- application directories following the `PlunkDev/OpenRC` convention;
- synthetic ISO, ELF, SHA-256, disc, WAD, bundle, 2FIP, boundary-table,
  MapArtV1, PS2 save-bundle, PS ADPCM, VAGp, SBlk/audio, scene-block,
  companion-WAD-index, and preparation tests that contain no copyrighted game
  data.

**Prepare game files** becomes available after the supported reference
executable is detected. **Play** remains disabled because the native runtime
does not exist yet.

## Build on Windows

Open a Visual Studio Developer PowerShell in the repository directory:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The executables will normally be located at:

```text
build/Debug/openrc-cli.exe
build/Debug/openrc-launcher.exe
```

Inspect a disc from the command line:

```powershell
build/Debug/openrc-cli.exe inspect local/ratchet-and-clank.iso
```

Inventory, prepare, and inspect its boot ELF:

```powershell
build/Debug/openrc-cli.exe inventory local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe toc local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe toc-assets local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe wad local/ratchet-and-clank.iso 100
build/Debug/openrc-cli.exe vagp local/ratchet-and-clank.iso 51 sample.wav
build/Debug/openrc-cli.exe boundary local/ratchet-and-clank.iso 259
build/Debug/openrc-cli.exe map-art local/ratchet-and-clank.iso 0 map-art.tga
build/Debug/openrc-cli.exe ps2-save local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe sblk local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe scene-blocks local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe companion-wads local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe wad-bundle local/ratchet-and-clank.iso 14365 162
build/Debug/openrc-cli.exe twofip local/ratchet-and-clank.iso 100 texture.tga
build/Debug/openrc-cli.exe prepare local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe elf path/to/prepared/files/SCES_509.16
```

The `wad-bundle` LBA and sector count above identify a container in the exact
PAL v2.00 reference image; they are not assumed for other revisions.
The `twofip` command accepts either a direct 2FIP global slot or a WadV1 slot
whose decoded payload is 2FIP. The optional output is created only when the
target path does not already exist.
`map-art` accepts a level ID from 0 through 18 and can create one 384x128 TGA
containing its three validated 2FIP layers side by side. `ps2-save` inventories
the reference build's PS2D memory-card resources without exporting game data.
`sblk` validates the selected level's SBlk item directory and PS2 ADPCM frame
bank, including shared sample references, one-shot/loop classification, and
loop boundaries. SBlk has no intrinsic sample-rate field in Hz: its signed
center-note/fine values are per-reference runtime tuning, so audio export will
require an explicit rate policy rather than assigning a guessed block rate.
`vagp` accepts a global VAGp TOC slot and optionally creates a canonical mono
PCM16 WAV. The export uses the asset's declared sample rate and excludes only
the zero lead-in and terminal control frame. Output paths are never
overwritten.
`scene-blocks` decodes primary-extent-0 subrange 10 for one level, validates
its neutral block directory and exact chain, and cross-checks the declared
count against the independent extent-3 table. It does not yet label those
blocks as terrain, collision, or models.
`companion-wads` validates the independent subrange-2 index into that decoded
buffer, checks every exact WadV1 range and zero alignment gap, and then
actually decodes all indexed WAD streams under an aggregate limit.

When no destination is supplied, prepared files are stored below
`%LOCALAPPDATA%\PlunkDev\OpenRC\games`. The original image is never modified.

Show OpenRC's user-data directories:

```powershell
build/Debug/openrc-cli.exe paths
```

## User data

On Windows, OpenRC stores roaming configuration in:

```text
%APPDATA%\PlunkDev\OpenRC
```

Machine-specific data, cache, extracted files, and logs live below:

```text
%LOCALAPPDATA%\PlunkDev\OpenRC
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap](docs/ROADMAP.md)
- [Reference build](docs/REFERENCE_BUILD.md)
- [Legal and project boundaries](docs/LEGAL.md)

## License status

No open-source license has been selected yet. Until one is added, the source is
not licensed for redistribution. This decision must be made before accepting
external contributions or incorporating GPL-licensed code such as Wrench.
