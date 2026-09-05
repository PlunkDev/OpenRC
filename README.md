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

Stage 1 inventory and the reusable foundation for the first playable slice are
both in progress. The repository currently provides:

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
- a bounded `SceneAnimationBankV1` parser for every local resource-block WAD
  run, covering camera-record cadence, actor animation frames, per-frame root
  transforms, and optional PAL five-language subtitle tails;
- a strict `RacGameplayBankV1` parser for both regional gameplay payloads on
  all 19 levels, exposing 36 named block ranges plus validated RAC1 moby-class
  IDs and 0x78-byte static-instance placements, dense per-instance PVar
  ownership and opaque ranges, typed Moby-link/relative-pointer fixups, and
  bounded TIE/shrub class lists with their complete instance matrices;
- a strict `RacLevelCoreIndexV1` parser that links every gameplay Moby class in
  exact count/order to its local model range, shared gadget WAD, texture slots,
  and placement count on all 19 levels, and independently links the TIE and
  shrub class tables to their local assets and texture slots;
- exact preservation of the 256-slot Ratchet sequence table, including empty
  slots and aliases, with one bounded range per unique sequence asset;
- a bounded regular Ratchet-frame parser and compiler-side pose decoder for
  signed XYZW rotations, sparse local/terminal scale, sparse translation,
  hierarchy, and inverse bind, verified across all 33,977 PAL frames;
- a source-layout-aware ordinary-Moby animation path that normalizes
  class-relative frame addresses and compiles complete source-addressed clip
  banks against the exact decoded rig without inventing state semantics;
- a bounded `RacMobyClassV1` parser for all local level models and the 21 shared
  companion-bank models, including packet directories, animation/skeleton
  ranges, and regular high/low/metal packet ownership;
- bounded regular Moby packet recovery for VIF texture coordinates, strip
  indices, AD-GIF texture switches, delayed vertex-cache indices, packet-local
  vertices, strips, and triangle topology;
- complete regular high/low LOD assembly with the 512-entry vertex cache and
  current texture carried across packets, resolving every inherited duplicate
  on all 19 reference levels;
- a bounded level Moby asset loader and static-instance scene builder that
  applies the verified `T * S * Rz * Ry * Rx` placement transform while
  explicitly skipping animated classes until bind transforms are recovered,
  while retaining per-class material slots, per-vertex UVs, and contiguous
  resolved material batches;
- bounded RAC1 TIE high-LOD packet, vertex, strip, material, and triangle
  recovery plus a scene builder that applies each complete column-major
  instance matrix without treating its special final word as perspective;
- bounded RAC1 level tfrag, Moby, TIE, and shrub texture-bank decoding from raw
  GS RAM and the decoded level core, including linear PSMT8 pixels, GS CLUT
  permutation, PS2 alpha expansion, RGBA output, and single-texture TGA export;
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
- versioned binary `PreparedGameV2` and `LevelPackageV1` containers with
  canonical serialization, SHA-256 integrity, source provenance, bounded
  readers, and explicitly ordered replace/remove mod overlays;
- an authoritative RAC1 collision parser covering the sparse world grid,
  packed triangle/quad geometry, raw surface IDs, and hero-only barriers,
  validated with one implementation across all 19 reference levels;
- deterministic compilation of that source data into an exact-Q6
  `CollisionWorldV1`, canonical binary collision payload, rebuilt uniform-grid
  index, and bounded native collision queries;
- a neutral `LevelBootstrapV1` carrying the authored player spawn and absolute
  death plane, plus a planet-agnostic foundation compiler that packages
  bootstrap and collision resources with complete source/generated provenance;
- a versioned, planet-independent `RenderSceneV1` resource with canonical
  textures, materials, meshes, instances, bounded binary I/O, and a native
  D3D11 staging path;
- a versioned `ActorLibraryV1` resource whose semantic rig/model keys,
  content digests, high-LOD skinned meshes, decoded textures, materials, and
  exact bounded skin weights are independent of RAC class IDs and PS2 packet
  addresses;
- a versioned `ActorAnimationBankV1` resource with semantic clip/rig keys,
  exact rig-content binding, neutral local joint transforms, source cadence,
  loop/clamp policy, bounded canonical I/O, and no RAC slot numbers or packed
  frame records at runtime;
- a versioned component-table `EntitySceneV1` resource with stable authored
  IDs and semantic archetype/model keys, including the player-slot to actor
  relationship used by the package-only runtime;
- a versioned `GameplaySceneV1` resource whose authored-entity references,
  local overlap spheres, semantic item keys, amounts, canonical binary I/O, and
  explicit bounds contain no RAC class IDs or source-format dispatch;
- a versioned `DestructibleSceneV1` resource with authored-entity references,
  bounded local hit spheres, health, accepted damage channels, and ordered
  semantic item drops, likewise independent of RAC/PS2 formats;
- a versioned `ActorBehaviorSceneV1` foundation with exact model, rig, and
  animation-content contracts, typed per-instance state, explicit entity
  relationships, initial channel bindings, scoped RNG, canonical binary I/O,
  package attachment, and transactional native execution;
- reusable bind-pose palette construction, CPU linear-blend skinning, and
  static actor-to-RenderScene baking with general affine joint transforms and
  inverse-transpose normal handling, plus a position-only path that preserves
  intentional singular animation scales without inventing a normal policy;
- generic ordinary-Moby actor compilation and runtime presentation: Veldin
  class 749 currently contributes one shared textured 53-joint model, all eight
  source sequences, and 16 independently posed/transformed actor instances;
- an in-progress deterministic native-game compiler path for all 19 reference
  levels, targeting exactly eight neutral resources per level—collision,
  bootstrap, render scene, actor library, actor animations, entity scene,
  gameplay scene, and destructible scene—in one transactional PreparedGameV2
  installation;
- a hardened PreparedGameV2 filesystem reader and transactional publisher for
  explicit caller-supplied level packages, with no implicit mod discovery;
- a planet-agnostic game session/entity world, quantized replay-input boundary,
  and integer fixed-step scheduler for exact 50/60 Hz simulation;
- a deterministic character controller and player simulation with movement,
  gravity, jumping, ground/wall handling, checkpoints, and fall reset, together
  with a source-independent loader for resolved foundation packages;
- a deterministic authored-entity gameplay runtime with transactional scene
  loads, fixed-tick collectible overlap, neutral damage/destruction events,
  render-instance visibility, and generic `u64` semantic item totals that
  survive level reloads;
- a source-independent fixed-tick combat producer whose current primary wrench
  profile drives bounded melee damage capsules without placing RAC class logic
  in the runtime;
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
- a profile-bound compiler/diagnostic SceneBlock loader that revalidates the
  user's ISO against its prepared boot ELF before exposing reusable decoded
  level data on the source side of the package boundary;
- a package-only graphical runtime path that mounts PreparedGameV2, verifies
  the selected level and its neutral resources, and renders without reopening
  the source ISO or boot ELF, with deterministic fixed-step movement, collision,
  jumping, fall/reset handling, a third-person camera, and Ratchet's textured
  high-LOD model CPU-skinned from neutral idle, walk, and run clips at the
  simulated player transform, plus independently addressable world actors,
  neutral Bolt collection and destructible state, semantic inventory,
  static-instance visibility, and a primary melee action;
- recognition of the PAL (`SCES-50916`) reference executable and detection of
  the NTSC-U/C (`SCUS-97199`) release;
- a native Windows launcher with disc inspection, asynchronous one-time
  compilation of all 19 levels into a content-addressed PreparedGameV2
  installation, progress, cancellation, persisted installation identity, and a
  package-only Play action that starts the verified adjacent runtime;
- CLI-only compilation/publication of all 19 collision/bootstrap foundation
  packages and deterministic movement smoke paths that can consume either one
  `.orlvl` file or a published PreparedGameV2 root without reopening the ISO;
- application directories following the `PlunkDev/OpenRC` convention;
- synthetic ISO, ELF, SHA-256, disc, WAD, bundle, 2FIP, boundary-table,
  MapArtV1, PS2 save-bundle, PS ADPCM, VAGp, SBlk/audio/WAV, decoded-WAD
  inventory/probes, RAC gameplay/level-core and tfrag/Moby/TIE/shrub texture
  tables, Moby and TIE class/packet/LOD geometry, static scene transforms and
  material-slot mapping, neutral collision compilation/I/O and queries,
  level-bootstrap/foundation compilation, PreparedGameV2 filesystem
  loading/publication, deterministic character/player simulation, runtime
  foundation/content loading,
  ActorLibrary/ActorAnimation/EntityScene/GameplayScene/DestructibleScene
  canonical I/O and package attachment, actor pose/skinning and static
  bind-pose baking,
  semantic player-actor resolution, deterministic collectible/destructible
  state and primary-combat timing,
  portable-PE validation,
  Ratchet sequence/pose, scene-animation/subtitle, EE/R5900 boundaries,
  scene-block, scene-block
  VIF/VU execution and phase grouping, DVP VU microprogram decoding/execution,
  companion-WAD-index, and preparation tests that contain no copyrighted game
  data.

**Prepare native game** becomes available after the supported disc is detected.
It verifies and extracts the required source data, compiles all 19 levels once,
and atomically publishes a content-addressed installation below
`%LOCALAPPDATA%\PlunkDev\OpenRC\prepared-v2`. The Launcher remembers the exact
manifest. Later **Play** runs level 0 from that installation alone: the ISO and
boot ELF are not passed to or reopened by the runtime.

Prepared-game mode is now an early playable Veldin prototype. Use `W/A/S/D` to
move relative to the camera, the arrow keys to rotate and pitch it, `Space` to
jump, `F` or the left mouse button for the primary wrench attack, and `R` to
reset to the authored checkpoint. An XInput-compatible controller uses the
left/right sticks for movement/camera, `A/Cross` for jump, and `X/Square` for
the current primary action. The runtime resolves Ratchet through neutral
entity-scene, actor-library, and actor-animation keys, CPU-skins his textured
high-LOD mesh, and places it at the deterministic player transform. The
compiler now preserves every occupied Ratchet sequence slot under a numeric
`actors/ratchet/source-sequence/NNN` key (134 clips on Veldin), rather than
publishing guessed gameplay names. The recovered ordinary grounded states now
identify slot 0 as idle, slot 3 as slow movement, and slot 4 as full movement.
Slots 3 and 4 switch from actual horizontal pace with strict `> 2.35` and
`< 1.90` hysteresis and preserve the source frame through the recovered
cross-clip remap. Their PAL 50 Hz timing advances on the 60 Hz fixed simulation
with an integer accumulator. The complete state-0/state-2 entry and stop guards
are not connected yet. Airborne
movement deliberately holds the last sampled grounded pose until its source
sequence mapping is proven; the primary action likewise has prototype gameplay
timing and a melee hit volume but not the source wrench model, animation, or
finished presentation yet. Keyboard input and dependency-free XInput polling
are connected at the same deterministic boundary. The latter preserves the
full signed stick magnitude, so partial left-stick travel reaches simulation
instead of becoming a digital press. No guessed dead zone or speed curve is
applied by the platform adapter. After that replay boundary, the runtime uses
the recovered DualShock byte response: center 127, dead-zone magnitude 48,
then `(abs(delta) - 48) / 76` clamped to one. The standard grounded player
path then reproduces the source's automatic two-pace choice: any non-zero
radial magnitude below `0.82` selects the `0.9` slow coefficient, while
`0.82` and above selects `5.7`. There is no separate run button. Keyboard
movement represents full stick travel, while a controller can therefore make
Ratchet walk slowly with a light tilt. The unquantized source response and
selected pace remain attached to every fixed gameplay tick. State-specific
speed exceptions, the source acceleration/turning modifiers, and the complete
movement-state transitions remain part of the player-state reconstruction.

The current eight-resource profile is verified end to end for the supported
PAL v2.00 image: fresh compilation and exact validation cover all 19 levels,
a second preparation reuses the same verified cache, and package-only gameplay
smokes pass for both Veldin level IDs. The exact profile validation includes
the build-specific complete animation-slot count for every level, numeric slot
keys, rig digest, joint counts, and the mounted runtime animation bank. The
D3D11 smoke additionally proves that a real mounted
Bolt Crate is submitted while visible, receives the neutral primary attack,
grants its drop, and is absent from the next rendered frame. The map still lacks
most animated and specialized object families, finished attack presentation,
enemy AI/combat/damage behavior, menus, the original camera behavior, and
several progression interactions, so Veldin is not yet a complete playable
level. The first enemy-shaped source class is presented from package data, but
is deliberately not called gameplay-complete yet.

There is one Launcher and one runtime. The prepared installation is a
versioned local cache made from the player's disc, not another client. The
Launcher validates the exact compiler/profile identity before enabling Play;
an old, incomplete, or damaged cache is rejected and the normal **Prepare
game** flow rebuilds that same local installation. Only compiler and diagnostic
tools accept ISO/ELF inputs. The public runtime accepts a prepared root and
level ID only.

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
the `libc++*`, `libunwind*`, `libgcc*`, `libstdc++*`, or `libwinpthread*`
compiler-runtime families. Do not copy those DLLs into that directory.
Intermediate build directories may contain stale diagnostic executables and are
not the distribution folder. Public MinGW executables are always checked after
linking; test/developer executables receive the same check when the static-build
policy is enabled. The Launcher also rejects an adjacent runtime with the wrong
architecture or a dynamic compiler-runtime dependency before asking Windows to
start it. Always launch the player-facing build from `build-portable`;
`build-werror` and other build directories are development workspaces rather
than runnable packages.

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

This is the package boundary used by the Launcher and intended for future mod
tooling. It contains neutral OpenRC resources rather than copied source WAD
records. The current compiler emits eight resources in every level package:
`world/collision`, `world/bootstrap`, `world/render-scene`, `actors/library`,
`actors/animations`, `world/entities`, `world/gameplay`, and
`world/destructibles`. RAC/PS2 decoding stops in the compiler; the runtime
resolves documented semantic keys, authored IDs, and versioned resource schemas
instead of disc class IDs, animation slots, or offsets.

For the supported RAC1 profile, the compiler-only adapter currently treats
static Moby class 13 as the Bolt collectible using high-confidence community
metadata. It bakes the high-LOD model in bind pose into ordinary static render
instances and links those instances to neutral entities and overlap
collectibles. Each grants `amount = 1` to `openrc.currency/bolts`; that amount
is an explicit OpenRC policy, not a recovered per-placement value. The current
eight-resource profile additionally maps static Moby class 500 to neutral Bolt
Crates with one health and one Bolt drop as explicit OpenRC policy. All-level
preparation/reuse, package-only Veldin gameplay, and D3D11 visible-to-destroyed
crate smoke have passed. Ratchet's confirmed idle/slow/full grounded animation
is now packaged and played. Veldin class 749 is also packaged as 16 independent
skeletal actor instances with all source animations; its executable-proven
state-0 entry sequence is classified in
the neutral animation bank and sampled without hard-coded runtime dispatch on
the RAC class or slot. Wrench and airborne animation, original pickup/destruction
presentation, enemy behavior, menus, and the remaining gameplay systems are
still in progress.

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

The following source-recovery results document compiler-side evidence, not a
second player or a public runtime mode. The old direct ISO/ELF D3D11 diagnostic
entrypoint has been removed; `openrc-runtime` accepts only neutral prepared
packages. For the confirmed entry-16 path, the retained recovery pipeline
follows the game's VU-memory
indirection and recovers signed source XYZ for each GS vertex, while using only
the already-decoded emitted triangle topology. It also snapshots both GS
texture contexts, decodes TEX0/CLAMP writes, converts complete STQ to logical
`S/Q,T/Q`, and accepts the corpus-verified table-index TEX0 convention. When
PRMODE does not identify a context, the bounded RAC1 policy accepts a material
only if exactly one context was programmed. On reference Veldin, 460 records
produce 325 decoded GS streams; 263 normally completed records yield exact
source geometry, merging to 22,428 vertices and 18,660 triangles. Another 135
records emit no XGKICK in this pass and 62 stop on still-indeterminate runtime
state, so this is an honest supported-record aggregate rather than a claim that
every gameplay render pass is reconstructed. The original record-0 proof still
contains 80 submitted vertices, 73 unique descriptors, and 71 unique source
positions. The source-side tooling decodes Veldin's 78-entry tfrag bank (878,592 indexed
pixels) and submits contiguous recovered material batches through the D3D11
texture path; any unresolved material remains a wireframe instead of receiving
a guessed image. The former isolated debug orbit and GS-output comparison were
development aids for this evidence. The DVP VU layer still does
not emulate bit-exact VU floating point or live PATH1 arbitration, and this is
not yet a classified collision or playable scene representation.

For full-level entry-16 recovery, the compiler-side pipeline additionally loads
the independently indexed gameplay and level-core Moby assets. It carries the 512-entry vertex
cache and texture state across each high-LOD packet sequence, applies each
static placement's verified `T * S * Rz * Ry * Rx` transform, then explicitly
converts world coordinates to the current SceneBlock diagnostic domain at
1,024 raw ITOF0 units per world unit before the bounded merge. On Veldin this
adds 133 placements from five non-animated classes: 20,370 compacted vertices
and 13,130 triangles beside
the existing 22,428/18,660 recovered SceneBlock batch. They are now submitted
through contiguous material batches with perspective-correct
UV interpolation, decoded RGBA base textures, alpha-zero rejection, and depth
testing. Explicitly untextured or unresolved terrain batches remain diagnostic
wireframes. Metal/bangle meshes and 153 animated placements still wait for
their respective decoders and bind transforms. Ten Veldin placements reference
external/zero model ownership and remain skipped.

The same full-level path now decodes all 63 Veldin TIE classes and applies the
complete column-major matrix of every one of their 1,114 placements. This adds
500,510 compacted vertices and 396,708 triangles, resolved through 131 entries
in the separate TIE texture bank. The renderer accepts explicit ordered texture
regions, so equal numeric texture indices in the terrain, Moby, and TIE tables
cannot alias each other. Shrub metadata and its 70 textures are inventoried,
but the 1,697 shrub placements still await their specialized geometry decoder.

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
- [ActorLibraryV1 resource](docs/ACTOR_LIBRARY_V1.md)
- [EntitySceneV1 resource](docs/ENTITY_SCENE_V1.md)
- [GameplaySceneV1 resource](docs/GAMEPLAY_SCENE_V1.md)
- [DestructibleSceneV1 resource](docs/DESTRUCTIBLE_SCENE_V1.md)
- [ActorBehaviorSceneV1 resource](docs/ACTOR_BEHAVIOR_SCENE_V1.md)
- [RAC Ratchet sequence and pose recovery V1](docs/RAC_RATCHET_POSE_V1.md)
- [PreparedGameV2 and LevelPackageV1](docs/PREPARED_GAME_V2.md)
- [Reference build](docs/REFERENCE_BUILD.md)
- [Legal and project boundaries](docs/LEGAL.md)

## License status

No open-source license has been selected yet. Until one is added, the source is
not licensed for redistribution. This decision must be made before accepting
external contributions or incorporating GPL-licensed code such as Wrench.
