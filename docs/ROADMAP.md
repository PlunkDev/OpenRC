# OpenRC roadmap

This roadmap describes technical milestones rather than release dates.

## Stage 0 — Foundation

- [x] C++20 and CMake project structure
- [x] ISO 9660 and `SYSTEM.CNF` inspection
- [x] PAL and NTSC-U/C build recognition
- [x] native Windows launcher shell
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

## Stage 2 — Native level viewer

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
- [ ] parse and classify terrain, collision, metal/bangle geometry, skeletal
  bind poses, and animation transforms
- [ ] render a classified, textured Veldin scene in a native window
- [ ] compare geometry and transforms with reference captures

## Stage 3 — First playable vertical slice

- load Ratchet and his animations
- implement controller input and camera behavior
- reconstruct movement, collision, and basic interactions
- implement one weapon and one enemy type
- load and save isolated test state

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
