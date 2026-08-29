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
- a streaming whole-disc decoded-WAD inventory with SHA-256 deduplication,
  typed source provenance, strict known-format probes, and explicit
  recognized/unknown/ambiguous results;
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
- bounded SBlk mono PCM16 WAV export for one selected physical block, requiring
  either an explicit caller-supplied rate or the named 48 kHz SPU diagnostic
  policy and preserving loop sample bounds;
- a strict VAGp V1 parser with owned metadata, decoded content ranges, and
  bounded mono PCM16 WAV export using the rate stored in each asset;
- a neutral `SceneBlockDirectoryV1` parser for the large decoded per-level
  container, with exact chained block envelopes, eight neutral remainder
  sections, owned trailing data, and bounded zero-copy VIF command metadata
  for the verified stream spanning sections 0-4, plus conservative execution
  into a neutral 1024-qword VU1 memory snapshot with ordered write provenance
  and explicit indeterminate lanes, followed by exact neutral control-to-UNPACK
  phase grouping with inherited state and disjoint destination runs;
- a bounded `CompanionTerminalWadIndexV1` parser that validates the 21 aligned
  terminal WadV1 records shared by every reference level;
- streamed SHA-256 inventory and extraction into an immutable prepared-game
  directory with a deterministic manifest;
- ELF32/MIPS span/path parsing with program/section inventory, a typed DVP
  overlay table mapped from LMA/VMA records to the real code bytes, and bounded
  IOP/IRX module, relocation, and import metadata;
- a bounded EE/R5900 executable-region and control-transfer inventory with raw
  instruction provenance, direct/indirect call sites, kernel `SYSCALL` sites,
  and exact adjacent constant-selector wrapper proofs;
- a bounded neutral DVP VU microprogram decoder which preserves both raw words
  and explicit unknown operations while exposing the recognized upper/lower
  instructions required by overlay group `55907`, upper flags and operands,
  wrapped direct-branch targets, and typed control-flow and VU1-memory-access
  inventory including XTOP and XGKICK sites;
- a bounded functional VU1 execution layer with explicit partially known
  register/RAM state, paired upper/lower reads, branch and E delay slots,
  confirmed STATUS/CLIP/Q latencies, conservative VU memory operations, and
  synchronous bounded XGKICK/GIFtag packet snapshots;
- exact SceneBlock task initialization and record execution with carried
  BASE/OFFSET/DBF, VIF control state, both input banks, and the caller-provided
  four-qword frame transform;
- a stateful bounded GIF/GS stream decoder with primitive assembly, vertex
  attributes, raster-context snapshots, and optional auto-fit wireframe TGA
  export from real XGKICK output;
- exact entry-16 source-geometry recovery through each record's matched V4-8
  index stream and index-to-descriptor-to-position chain, retaining VIF write
  provenance and validating every recovered RGBA value against decoded GS;
- a profile-bound SceneBlock runtime loader that revalidates the user's ISO
  against its prepared boot ELF before exposing reusable decoded level data;
- a native Windows D3D11 level-viewer window that independently executes and
  merges every supported SceneBlock record in the selected level, defaults to
  an auto-fit recovered-source 3D orbit view, retains the aggregate decoded GS
  projection for comparison, and falls back from hardware rendering to WARP;
- recognition of the PAL (`SCES-50916`) reference executable and detection of
  the NTSC-U/C (`SCUS-97199`) release;
- a native Windows launcher with disc inspection, asynchronous Prepare,
  progress, cancellation, and a Play action that starts the verified adjacent
  runtime with the prepared game files;
- application directories following the `PlunkDev/OpenRC` convention;
- synthetic ISO, ELF, SHA-256, disc, WAD, bundle, 2FIP, boundary-table,
  MapArtV1, PS2 save-bundle, PS ADPCM, VAGp, SBlk/audio/WAV, decoded-WAD
  inventory/probes, EE/R5900 boundaries, scene-block,
  scene-block VIF/VU execution and phase grouping, DVP VU microprogram
  decoding/execution, companion-WAD-index, and preparation tests that contain
  no copyrighted game data.

**Prepare game files** becomes available after the supported reference
executable is detected. After preparation succeeds, **Play** starts the native
level viewer with level 0, every record, and SceneBlock entry pair 16. Records
are executed independently from the same validated entry-0 state; records that
stop diagnostically after a complete GS packet and non-drawing records are
counted rather than allowed to abort the aggregate.

The initial viewer opens in **recovered source 3D (debug orbit)** mode. Drag the
left mouse button or use the arrow keys to orbit, use the mouse wheel or `+/-`
to zoom, press `R` to reset, and press `Tab` to compare the decoded GS 2D
output. This camera belongs only to the diagnostic viewer; it is not presented
as Ratchet & Clank's original gameplay camera.

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
build/Debug/openrc-runtime.exe
```

For a self-contained x64 package built with the repository's bundled
LLVM-MinGW tools, run:

```powershell
.\scripts\build-portable.ps1
.\build-portable\openrc-launcher.exe
```

`build-portable` is atomically replaced with a clean runnable package containing
only the matching CLI, launcher, and runtime. The packaging step verifies that
all three files are AMD64, have unchanged hashes, and do not dynamically import
the C++ or unwind runtimes. Do not copy `libc++.dll` or `libunwind.dll` into that
directory. Intermediate build directories may contain stale diagnostic
executables and are not the distribution folder.

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
build/Debug/openrc-cli.exe wad-payload-inventory local/ratchet-and-clank.iso wad-payloads.tsv
build/Debug/openrc-cli.exe wad-families local/ratchet-and-clank.iso wad-families.tsv
build/Debug/openrc-cli.exe wad-subtitles local/ratchet-and-clank.iso 604
build/Debug/openrc-cli.exe wad-payload-export local/ratchet-and-clank.iso 604 payload-604.bin
build/Debug/openrc-cli.exe vagp local/ratchet-and-clank.iso 51 sample.wav
build/Debug/openrc-cli.exe boundary local/ratchet-and-clank.iso 259
build/Debug/openrc-cli.exe map-art local/ratchet-and-clank.iso 0 map-art.tga
build/Debug/openrc-cli.exe ps2-save local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe sblk local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe sblk-wav local/ratchet-and-clank.iso 0 0 sample.wav spu-native-48000
build/Debug/openrc-cli.exe sblk-wav local/ratchet-and-clank.iso 0 0 sample-22050.wav caller-supplied-hz 22050
build/Debug/openrc-cli.exe scene-blocks local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe scene-block-vu-run local/ratchet-and-clank.iso path/to/prepared/files/SCES_509.16 0 0 16 scene-block.tga
build/Debug/openrc-cli.exe companion-wads local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe wad-bundle local/ratchet-and-clank.iso 14365 162
build/Debug/openrc-cli.exe twofip local/ratchet-and-clank.iso 100 texture.tga
build/Debug/openrc-cli.exe prepare local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe elf path/to/prepared/files/SCES_509.16
build/Debug/openrc-cli.exe r5900-boundaries path/to/prepared/files/SCES_509.16
build/Debug/openrc-cli.exe dvp-vu path/to/prepared/files/SCES_509.16 2,6,8,10,14,16,20 11,12,13,14,15,16,17,18
build/Debug/openrc-cli.exe dvp-vu-run path/to/prepared/files/SCES_509.16 2 11,12,13,14,15,16,17,18 0
build/Debug/openrc-runtime.exe --disc-image local/ratchet-and-clank.iso --boot-executable path/to/prepared/files/SCES_509.16 --level 0 --record all --entry-pair 16
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
center-note/fine values are per-reference runtime tuning. `sblk-wav` therefore
requires either `caller-supplied-hz <hz>` or the explicitly named
`spu-native-48000` diagnostic policy rather than assigning a guessed block
rate. It decodes only the selected block's declared content frames and never
overwrites an existing output.
`vagp` accepts a global VAGp TOC slot and optionally creates a canonical mono
PCM16 WAV. The export uses the asset's declared sample rate and excludes only
the zero lead-in and terminal control frame. Output paths are never
overwritten.
`scene-blocks` decodes primary-extent-0 subrange 10 for one level, validates
its neutral block directory, exact chain, eight-section layouts, and bounded
VIF stream across sections 0-4, then cross-checks the declared count against
the independent extent-3 table. The report includes exact opcode, payload,
alignment, and conservative VU1 write totals. VU addresses are reported
relative to an explicitly supplied diagnostic `TOPS=0`; V3-16 W follows the
hardware V4-path lookahead/boundary-zero behavior while fill lanes remain
unknown. It also reports exact neutral command phases and their
destination coverage without treating the two corpus-observed phase skeletons
as an acceptance grammar. It does not yet label those blocks as terrain,
collision, or models.
`scene-block-vu-run` reconstructs the confirmed task preamble and one selected
record packet, seeds both frame-transform banks, executes overlay group `55907`,
and decodes its complete ordered XGKICK stream into GS writes, vertices, and
primitive emissions. Its optional TGA is an auto-fit diagnostic wireframe. The
current identity frame transform is deterministic debug input, not a claim to
reproduce the game's live camera.

The native D3D11 window is still a diagnostic level viewer, not a playable
runtime. For the confirmed entry-16 path it follows the game's VU-memory
indirection and recovers signed source XYZ for each GS vertex, while using only
the already-decoded emitted triangle topology. On reference Veldin, 460 records
produce 325 decoded GS streams; 263 normally completed records yield exact
source geometry, merging to 22,428 vertices and 18,660 triangles. Another 135
records emit no XGKICK in this pass and 62 stop on still-indeterminate runtime
state, so this is an honest supported-record aggregate rather than a claim that
every gameplay render pass is reconstructed. The original record-0 proof still
contains 80 submitted vertices, 73 unique descriptors, and 71 unique source
positions. An isolated debug orbit can inspect the merged 3D mesh, while `Tab`
retains the aggregate GS-output comparison. The DVP VU layer still does not
emulate bit-exact VU floating point or live PATH1 arbitration, and the geometry
is not yet
classified as terrain, collision, or models.

`dvp-vu` accepts comma-separated decimal VU pair addresses and ELF overlay
section indices, with at most 128 values in either list. The example selects
seven decoded dispatcher entrypoints and all eight chunks of overlay group
`55907`. The SceneBlock task is confirmed to call entries
`6,8,10,14,16,20` in that program; its later MSCAL 2 follows an upload of the
separate group `903379`.

`dvp-vu-run` remains the intentionally incomplete microcode-only diagnostic.
It supplies the requested VIF1 TOP but leaves other registers and RAM
indeterminate and returns a nonzero code for an indeterminate, unsupported,
unmapped, or limit termination. Exact task execution belongs to
`scene-block-vu-run`; TOP is never silently substituted with VIF UNPACK TOPS.

`companion-wads` validates the independent subrange-2 index into that decoded
buffer, checks every exact WadV1 range and zero alignment gap, and then
actually decodes all indexed WAD streams under an aggregate limit.

`wad-payload-inventory` streams all 5069 decoded WadV1 observations reachable
from the PAL reference disc's global catalog and tail bundle, local WAD runs,
primary records, bundle children, and companion banks. It retains metadata and
hashes rather than asset bytes, deduplicates probes by decoded SHA-256, and can
write one TSV row per observation, including the exact parent observation for
nested records. Unknown is a first-class result; a parser limit or foreign
exception is not silently converted into a format match. Scene-directory
probing has a separate 4096-record cap, safely above the PAL corpus maximum of
2144; subtitle banks likewise have a separate 4096-entry cap, so a byte
envelope cannot imply an unbounded metadata allocation.

`wad-families` profiles every unique decoded payload in the same streaming pass
and groups exact V1 structural keys. The family ID is deterministic but remains
a reverse-engineering candidate, not a claimed semantic format. Its TSV ranks
unknown coverage on Veldin, retains representative hashes and bounded sampling
diagnostics, and reports classification, source, and per-level counts. Local
WAD provenance keeps the resource block and its two run lanes separate.

`wad-subtitles` inspects one explicitly selected unique payload as a strict
`LocalizedSubtitleBankV1`. The PAL format contains bounded 16-byte timing rows,
five relative text offsets in EN/FR/DE/ES/IT order, a fixed sentinel, NUL
termination, and minimum zero padding. Text bytes are percent-escaped rather
than assigned an unproven Unicode code page. `wad-payload-export` can copy one
selected decoded payload to a new local file for reproducible diagnostics; it
never overwrites an existing file. On the PAL v2.00 corpus the new strict probe
recognizes 70 unique subtitle banks with no ambiguous classifications.

`r5900-boundaries` excludes the ELF's DVP/VU code sections and inventories only
file-backed EE executable words. It reports typed jumps, branches, calls,
returns, delay-slot coverage, raw `SYSCALL` codes, and constant `$v1` selector
proofs. This is not yet a recovered function map or call graph.

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
