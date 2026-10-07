# OpenRC command-line reference

`openrc-cli` exposes the compiler and diagnostic tools. Only these tools read the
disc image or boot ELF; the runtime accepts prepared packages only. Build the
executables first (see [Building for development](../README.md#building-for-development)).
For what each component does, see [Components](COMPONENTS.md).

## Contents

- [Disc and format inspection](#disc-and-format-inspection)
- [Native foundation packages](#native-foundation-packages)
- [Full native game preparation](#full-native-game-preparation)
- [Command notes](#command-notes)
- [User-data paths](#user-data-paths)

## Disc and format inspection

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
build/Debug/openrc-cli.exe wad-scene-animation local/ratchet-and-clank.iso 661
build/Debug/openrc-cli.exe wad-gameplay local/ratchet-and-clank.iso 706
build/Debug/openrc-cli.exe wad-moby-class local/ratchet-and-clank.iso 707
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
build/Debug/openrc-cli.exe level-core local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe level-collision local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe level-player-smoke local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe level-moby-scene local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe level-tfrag-texture local/ratchet-and-clank.iso 0 40 veldin-tfrag-040.tga
build/Debug/openrc-cli.exe level-moby-texture local/ratchet-and-clank.iso 0 0 veldin-moby-000.tga
build/Debug/openrc-cli.exe companion-wads local/ratchet-and-clank.iso 0
build/Debug/openrc-cli.exe wad-bundle local/ratchet-and-clank.iso 14365 162
build/Debug/openrc-cli.exe twofip local/ratchet-and-clank.iso 100 texture.tga
build/Debug/openrc-cli.exe prepare local/ratchet-and-clank.iso
build/Debug/openrc-cli.exe elf path/to/prepared/files/SCES_509.16
build/Debug/openrc-cli.exe r5900-boundaries path/to/prepared/files/SCES_509.16
build/Debug/openrc-cli.exe dvp-vu path/to/prepared/files/SCES_509.16 2,6,8,10,14,16,20 11,12,13,14,15,16,17,18
build/Debug/openrc-cli.exe dvp-vu-run path/to/prepared/files/SCES_509.16 2 11,12,13,14,15,16,17,18 0
```

## Native foundation packages

The new native foundation path can be exercised separately from the graphical
viewer. Output roots must be absolute, and the single-package command creates a
new file rather than overwriting one:

```powershell
build/Debug/openrc-cli.exe level-foundation-package local/ratchet-and-clank.iso 0 local/veldin.orlvl
build/Debug/openrc-cli.exe level-package-smoke local/veldin.orlvl
$nativeRoot = Join-Path (Get-Location) "local/native-foundations"
build/Debug/openrc-cli.exe prepare-native-foundations local/ratchet-and-clank.iso $nativeRoot
build/Debug/openrc-cli.exe prepared-level-smoke $nativeRoot 0
```

`level-foundation-package` and `prepare-native-foundations` are compiler tools
and therefore read the supported ISO. `level-package-smoke` and
`prepared-level-smoke` read only the resulting neutral package data. These
headless smoke commands do not open the D3D11 viewer and are not an interactive
game mode.

## Full native game preparation

The current complete native-scene preparation compiles and transactionally
publishes all 19 levels. It needs the legally owned ISO and its already prepared
boot ELF only during compilation; smoke/loading and the graphical runtime use
the published root alone:

```powershell
$nativeRoot = Join-Path (Get-Location) "local/native-game-current"
build/Debug/openrc-cli.exe prepare-native-game local/ratchet-and-clank.iso path/to/prepared/files/SCES_509.16 $nativeRoot
build/Debug/openrc-cli.exe validate-native-game $nativeRoot
build/Debug/openrc-cli.exe prepared-native-level-smoke $nativeRoot 0
build/Debug/openrc-runtime.exe --prepared-root $nativeRoot --level 0
```

## Command notes

The `wad-bundle` LBA and sector count above identify a container in the exact
PAL v2.00 reference image; they are not assumed for other revisions.
The `twofip` command accepts either a direct 2FIP global slot or a WadV1 slot
whose decoded payload is 2FIP. The optional output is created only when the
target path does not already exist.
`level-tfrag-texture` and `level-moby-texture` select one table-local entry,
validate the complete corresponding bank against decoded core data and raw GS
RAM, and can create one normalized RGBA TGA without overwriting an existing
file. A Moby packet texture number is first resolved through that model class's
16 local slots; it is not treated as a global table index.
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

`level-core` links that same raw level index to its encoded and decoded asset
WAD, verifies that the gameplay and level-core Moby class lists have identical
counts and order, parses every bounded local and shared model core, and reports
static placement counts. For regular high/low packets it also reconstructs the
complete per-LOD transfer geometry, carrying the vertex cache and current
texture across packets. Missing inherited cache entries are hard errors; the
reference sweep resolves every one. Animated coordinates remain diagnostic
until skeleton bind transforms are applied.

`level-moby-scene` runs the same production loader and static-placement builder
used by native-package compilation without opening D3D. It reports the rendered,
animated, missing, and empty Moby placement counts plus compacted geometry
bounds, and separately reports decoded/instantiated TIE geometry and bounds.
Across all 19 levels it builds 9,122 static placements into 3,214,949 vertices
and 2,628,565 triangles; 6,237 animated and 873 external/zero-model placements
remain explicitly skipped.

`wad-moby-class` inspects one decoded standalone RAC1 object-model payload by
its inventory unique index. It validates the class header, packet ranges,
sequence offsets, optional skeleton metadata, shadow range, and regular
high/low packet geometry without treating metal packets as the same format.

`wad-payload-inventory` streams all 5,069 decoded WadV1 observations reachable
from the PAL reference disc's global catalog and tail bundle, local WAD runs,
primary records, bundle children, and companion banks. It retains metadata and
hashes rather than asset bytes, deduplicates probes by decoded SHA-256, and can
write one TSV row per observation, including the exact parent observation for
nested records. Unknown is a first-class result; a parser limit or foreign
exception is not silently converted into a format match. Scene-directory
probing has a separate 4096-record cap, safely above the PAL corpus maximum of
2144. Scene-animation probing separately permits at most 256 actor tracks and
65,536 total frame ranges; subtitle tails have a 4096-entry cap, so a byte
envelope cannot imply an unbounded metadata allocation. The PAL sweep
deduplicates to 4,605 payloads: 4,531 are recognized, 74 remain unknown, and
none is ambiguous. `SceneAnimationBankV1` accounts for exactly all 4,081 local
resource-block WAD-run payloads. `RacGameplayBankV1` accounts for exactly both
regional primary gameplay extents on all 19 levels, while `RacMobyClassV1`
accounts for 21 unique shared models across all 399 companion observations.
The remaining 74 unique unknown payloads occupy 31 structural candidate
families.

`wad-families` profiles every unique decoded payload in the same streaming pass
and groups exact V1 structural keys. The family ID is deterministic but remains
a reverse-engineering candidate, not a claimed semantic format. Its TSV ranks
unknown coverage on Veldin, retains representative hashes and bounded sampling
diagnostics, and reports classification, source, and per-level counts. Local
WAD provenance keeps the resource block and its two run lanes separate.

`wad-scene-animation` inspects one explicitly selected unique payload as a
strict complete `SceneAnimationBankV1`, reporting camera cadence, actor class
and scene-record metadata, frame ranges, root transforms, trailing bytes, and
the optional subtitle directory. `wad-gameplay` reports all 36 named gameplay
block ranges plus moby-class, static-moby-instance, and spawnable-moby counts.
`wad-subtitles` validates the same complete
scene before printing its PAL five-language tail. Subtitle rows contain timing
values and five relative text offsets in EN/FR/DE/ES/IT order; empty
directories, empty translated strings, and an opaque suffix after the logical
text envelope are retained without guessing a Unicode code page.
`wad-payload-export` can
copy one selected decoded payload to a new local file for reproducible
diagnostics; it never overwrites an existing file.

`r5900-boundaries` excludes the ELF's DVP/VU code sections and inventories only
file-backed EE executable words. It reports typed jumps, branches, calls,
returns, delay-slot coverage, raw `SYSCALL` codes, and constant `$v1` selector
proofs. This is not yet a recovered function map or call graph.

## User-data paths

When no destination is supplied, prepared files are stored below
`%LOCALAPPDATA%\PlunkDev\OpenRC\games`. The original image is never modified.

Show OpenRC's user-data directories:

```powershell
build/Debug/openrc-cli.exe paths
```
