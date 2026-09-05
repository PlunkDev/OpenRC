# OpenRC roadmap

This roadmap describes technical milestones rather than release dates.

The playable-runtime path is dependency-driven: unfinished broad Stage 1
inventory work is not automatically a gate for Stage 3. Only formats and code
paths required by the next verified gameplay subsystem become blockers. This
keeps the list honest without allowing discovery work to move the playable
finish line indefinitely.

## Stage 0 — Foundation

- [x] C++20 and CMake project structure
- [x] ISO 9660 and `SYSTEM.CNF` inspection
- [x] PAL and NTSC-U/C build recognition
- [x] native Windows launcher shell
- [x] statically link public LLVM-MinGW executables and reject wrong-architecture
  or compiler-runtime DLL families after linking, during portable publication,
  and before the Launcher starts the adjacent runtime
- [x] `PlunkDev/OpenRC` configuration and local-data directories
- [x] synthetic disc-parser tests
- [x] validate the inspector against the PAL v2.00 retail image
- [x] record the SHA-256 and basic metadata of the reference image
- [x] hash and inventory the boot executable and disc filesystem

## Stage 1 — Disc and executable inventory

- [x] extract regular ISO files into the local-data directory
- [x] preserve a deterministic manifest containing sizes, extents, and hashes
- [x] inventory ELF program headers, sections, and embedded DVP/VU overlays
- [x] parse the DVP overlay table and map every LMA/VMA record to real code
- [x] decode the confirmed VU microprogram under explicit bounds into neutral
  typed control-flow/access metadata while preserving raw and unknown data
- [x] export the boot ELF for analysis
- [x] establish the PAL v2.00 executable and image as the reference build
- [x] identify and validate the `DiscTocV1` index used outside regular ISO files
- [x] map level IDs 0–18 to their four primary raw-disc extents
- [x] identify the `WadV1` header and sector-padding contract
- [x] implement a bounded clean-room decoder for the WadV1 LZ stream
- [x] inventory the first decoded bundle of nested WAD and ELF records
- [x] expose and validate the remaining local TOC asset subtables
- [x] decode indexed `2FIP` textures and export normalized RGBA/TGA images
- [x] split the 38 decoded boundary-table payloads into seven bounded regions
- [x] parse the MapArtV1 record table and compose its three 2FIP preview layers
- [x] parse the PS2D memory-card icon and tagged save-template bundle
- [x] expose per-level SBlk descriptor groups and fixed-size item data
- [x] identify and validate the per-level SBlk PS2 ADPCM bank directory
- [x] implement a bounded common SBlk/VAGp PS ADPCM frame decoder
- [x] parse VAGp V1 metadata and export bounded canonical PCM16 WAV diagnostics
- [x] prove SBlk tuning is signed per-reference note/fine with no intrinsic Hz
- [x] add bounded SBlk WAV export with a mandatory named sample-rate policy
- [ ] expose local per-level VAGp music/speech selection and diagnostic export
- [ ] reconstruct voice selection plus pitch, volume, pan, loop, and lifetime inputs
- [x] inventory embedded IOP/IRX module metadata, relocations, and imports
- [x] validate the neutral per-level SceneBlockDirectoryV1 block chain
- [x] partition every scene-block remainder into eight verified neutral sections
- [x] expose the bounded VIF stream spanning scene-block sections 0-4
- [x] execute its bounded UNPACK writes into a conservative neutral VU1 snapshot
- [x] group VIF/VU commands into exact neutral phases with inherited state
- [x] reconstruct the exact SceneBlock task preamble and entry-specific packet
- [x] carry VIF/VU state across the double-buffered 328-qword input banks
- [x] identify and upload the caller's four-qword frame transform to both banks
- [x] execute a real record through normal VU termination and complete XGKICKs
- [x] decode ordered GIF/GS state into known vertices and emitted primitives
- [x] export the first auto-fit wireframe preview from real scene-block geometry
- [x] validate the companion index for the shared terminal WadV1 bank
- [x] inventory every decoded WAD payload as recognized, unknown, or ambiguous
- [x] group all unique decoded WAD payloads into deterministic structural
  candidate families and rank their unknown Veldin coverage
- [x] identify and strictly parse `SceneAnimationBankV1` across all 4,081 local
  resource-block WAD-run payloads
- [x] validate both header tags, camera cadence, actor frame tables, per-frame
  root transforms, and optional PAL five-language subtitle tails
- [x] identify the fixed 37-slot `RacGameplayBankV1` directory, all 36 named
  gameplay blocks, and the RAC1 moby class/instance anchors across both
  regional payloads for all 19 levels
- [x] identify all 21 shared companion-bank payloads as bounded
  `RacMobyClassV1` model cores across their 399 level observations
- [ ] identify the semantic formats inside the remaining 74 unique WAD
  payloads spanning 31 candidate families
- [x] inventory bounded EE/R5900 executable regions, control transfers, and
  PS2 system-call wrapper sites
- [ ] recover EE/R5900 function boundaries and construct the call graph
- [ ] inventory VU0/VU1 uploads, programs, entrypoints, and invocation sites
- [ ] inventory EE-to-IOP SIF/RPC and imported-module service boundaries
- [ ] inventory EE/VU-to-GIF/GS PATH, DMA, and MMIO boundaries

For the first playable slice, the remaining Stage 1 work is narrowed to the
EE/VU and payload families directly used by Ratchet's animations, the original
camera, one weapon, one enemy, and their scripted events. Ratchet's high-LOD
bind geometry, rig, exact skin weights, texture set, spawn, and checkpoint
inputs have already crossed the neutral package boundary. Broad audio, IOP,
call-graph, and unrelated unknown-payload coverage remains important for later
fidelity, but does not block movement on Veldin.

## Stage 2 — Native level viewer

This records the source-backed diagnostic viewer used to validate recovery.
That direct ISO/ELF entrypoint is no longer part of `openrc-runtime`; its
decoders remain compiler/diagnostic-side evidence feeding neutral packages.

- [x] bind a prepared ELF back to the selected ISO before native execution
- [x] open a D3D11 window and submit the first decoded SceneBlock triangle batch
- [x] connect launcher Play to the adjacent native runtime
- [x] recover entry-16 source XYZ through the game's descriptor/index stream
- [x] add an isolated debug orbit and decoded-GS comparison view
- [x] independently execute and merge all supported records for a raw level view
- [ ] recover the original per-frame camera transform from its EE caller
- [x] parse the top-level gameplay-instance bank and validate bounded
  0x78-byte RAC1 moby-record envelopes/counts
- [x] link level-core Moby tables to gameplay classes and parse every bounded
  local/shared `RacMobyClassV1` core on all 19 levels
- [x] recover regular high/low Moby packet-local vertices, texture switches,
  strips, and triangle topology across the complete PAL level corpus
- [x] resolve the 512-entry vertex cache and texture state across complete
  high/low LOD packet sequences on all 19 levels
- [x] load and transform static non-animated high-LOD Moby placements into the
  native Veldin debug view
- [x] decode every RAC1 Moby base-texture bank, normalize its PSMT8 CLUT/alpha,
  and preserve local-slot to global-texture material batches and UVs
- [x] submit static Moby material batches with perspective-correct UVs, decoded
  base textures, alpha rejection, and depth testing in the native viewer
- [x] decode level tfrag texture banks, retain TEX0/CLAMP context snapshots,
  convert recovered SceneBlock STQ, and render bounded terrain material batches
  in the native viewer
- [x] parse the level-core/gameplay TIE and shrub class-instance tables, decode
  every TIE high-LOD packet and texture bank, and apply complete instance
  matrices across all 19 levels
- [x] render all 1,114 Veldin TIE placements as a separately textured scene
  region alongside terrain and static Moby geometry
- [x] parse the authoritative RAC1 world/hero collision tree, preserve exact
  packed geometry and surface bytes, and validate the same parser on all 19
  PAL v2.00 levels
- [x] recover Ratchet's high-LOD bind-pose geometry, hierarchy, inverse binds,
  exact bounded skin weights, material slots, and base textures
- [x] preserve Ratchet's complete 256-slot sequence table, parse all regular
  frame partitions, and decode rotation/scale/translation palettes across all
  19 PAL levels
- [ ] parse and classify remaining terrain, metal/bangle geometry, and
  non-Ratchet actor animation transforms
- [ ] decode and render the remaining shrub, animated-Moby, and specialized
  Veldin asset families as one complete textured scene in the native window
- [ ] compare geometry and transforms with reference captures

## Stage 3 — First playable vertical slice

- [x] define versioned `PreparedGameV2` and `LevelPackageV1` containers with
  source provenance, integrity checks, and deterministic mod overlays
- [x] add a planet-agnostic `GameSession`, `LevelRequest`, entity world,
  quantized replay input, and exact fixed-step scheduler
- [x] compile authoritative RAC1 world/hero collision into a neutral exact-Q6
  `CollisionWorldV1` with canonical binary I/O, a rebuilt uniform-grid index,
  and bounded native movement/raycast queries
- [x] compile the authored player spawn and absolute death plane into neutral
  `LevelBootstrapV1`, then assemble both mandatory resources into a canonical
  planet-agnostic `LevelPackageV1` with source/generated provenance
- [x] add a hardened PreparedGameV2 filesystem reader and transactional
  all-level publisher for explicit packages, including rollback and strict
  rejection of path traversal, reparse points, stale hashes, and implicit mods
- [x] load a resolved collision/bootstrap package without an ISO and construct
  deterministic movement, jump, checkpoint, and fall-reset simulation from it
- [x] expose CLI package-only and published-root headless movement smoke paths
- [x] define canonical `RenderSceneV1` texture/material/mesh/instance data,
  bounded binary I/O, and a source-independent native D3D11 staging path
- [x] define canonical, bounded `GameplaySceneV1` overlap-collectible data with
  semantic item keys, authored-entity references, binary I/O, package
  attachment, and runtime package loading
- [x] add a compiler-only RAC collectible adapter and reusable bind-pose actor
  baking into static `RenderSceneV1` instances; class 13 uses high-confidence
  community Bolt metadata while `amount = 1` remains an explicit OpenRC policy
- [x] complete and verify the earlier six-resource transactional publication
  baseline for all 19 levels: collision, bootstrap, recovered textured render
  scene, actor library, entity scene, and gameplay scene
- [x] extend the earlier native compiler/profile contract to exactly seven
  resources by adding neutral `DestructibleSceneV1`, cross-resource validation,
  and stale six-resource cache rejection
- [x] extend the current native compiler/profile contract to exactly eight
  resources by adding neutral `ActorAnimationBankV1`, exact rig-digest and
  joint-count binding, and stale seven-resource cache rejection
- [x] mount and render a PreparedGameV2 level in the graphical runtime without
  reopening the source ISO or boot ELF
- [x] define canonical, bounded `ActorLibraryV1` and `EntitySceneV1` schemas
  with semantic keys, deterministic digests, binary I/O, package attachment,
  and cross-resource validation
- [x] load Ratchet's textured high-LOD bind model and rig through the generic
  actor-library path, resolve it through player/entity semantic bindings, and
  CPU-skin it at the simulated player transform
- [x] implement deterministic authored-entity materialization, fixed-tick
  capsule/sphere collection in canonical authored-ID order, collect-once events,
  overflow-safe semantic `u64` item totals, transactional reload, and snapshots
- [x] pass a fresh supported-image preparation/reuse smoke and a package-only
  Veldin D3D smoke proving collectible visibility and inventory end to end
- [x] add deterministic primary-combat timing, neutral melee damage,
  destruction/drop transactions, render hiding, and F/left-mouse input
- [x] add a compiler-only class-500 Bolt-Crate adapter with one shared model and
  stable per-placement entity/render/destructible identities
- [x] pass a fresh supported-image all-level preparation/reuse validation and
  package-only Veldin graphical smoke for the eight-resource
  crate/wrench/locomotion path
- [x] decode, package, select, and play Ratchet's confirmed idle slot 0, walk
  slot 3, and run slot 4 through the generic actor pipeline, advancing PAL
  50 Hz source updates on the 60 Hz fixed simulation with an integer cadence
  accumulator
- [ ] identify and connect Ratchet's airborne, landing, wrench, and remaining
  gameplay animation mappings without guessing source slots
- [x] connect Win32 keyboard input and a deterministic third-person camera to
  package-only fixed-step movement in the graphical runtime
- [x] package actor and entity data independently of RAC/PS2 source layouts
- [ ] reconstruct and package the original camera behavior
- [x] switch Launcher Prepare/Play to one-time all-level PreparedGameV2
  compilation and package-only runtime launch, persisting the exact native
  installation independently of the ISO
- [x] reconstruct one basic interaction through the neutral crate/wrench loop
- [ ] reconstruct one weapon and one enemy type
- [ ] load and save isolated test state
- [x] make package-only gameplay smoke on Veldin and at least one second planet
  a checked regression using the same published-package code path

The eight-resource collectible, crate/wrench, and grounded-locomotion profile
is verified on fresh prepared data across all 19 levels, including deterministic
reuse, package-only Veldin gameplay, exact animation-to-rig validation, and
visible-to-destroyed D3D11 smoke. Airborne ticks intentionally hold the last
grounded pose. This does not declare Veldin finished: airborne and wrench
animation, original pickup and destruction presentation, broader interactive
entities, one weapon/enemy loop, original camera behavior, menus, and full
scene-family coverage remain open.

## Stage 4 — Game-complete runtime

- reconstruct every planet and gameplay subsystem
- replace audio, cutscene, menu, save, and transition systems
- regression-test progression from a new game through the ending
- support PAL and NTSC timing differences

## Stage 5 — Modern platform features

- scalable resolution and aspect ratio
- high-frame-rate correctness
- controller remapping and accessibility options
- portable saves and launcher updates
- modding interfaces built on documented, original OpenRC formats

## OpenGOAL-like usability contract

Before the first public build is called usable, OpenRC must provide this flow:

1. The player selects a legally owned, supported disc image in the Launcher.
2. A deterministic asset compiler verifies it once and writes versioned,
   content-addressed native packages for every supported level.
3. **Play** starts the native runtime from those packages with no ISO path,
   ELF path, record number, or other developer flag.
4. There is one Launcher and one runtime; prepared packages are a versioned
   local cache, never a second client.
5. The Launcher rejects stale compiler/profile identities before Play, and the
   normal Prepare flow rebuilds incompatible packages without losing saves or
   user settings under the `PlunkDev/OpenRC` data directories.
6. Mods are explicit ordered overlays on documented OpenRC resource schemas;
   original game data is never redistributed or modified in place.
7. Portable builds contain no dynamic compiler-runtime DLL dependency, and a
   redistribution license, clean setup guide, diagnostics, and recovery path
   are present before inviting ordinary players.
