# OpenRC architecture

## Principles

1. The repository contains only original OpenRC source, tests, and metadata.
2. The user's disc image is read locally and is never modified.
3. Platform-independent code lives in `openrc_core`.
4. Reverse-engineering tools and the asset compiler share one bounded
   description of source disc builds and PS2 formats; the end-user runtime
   receives only neutral, versioned OpenRC resources.
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
├── strict RacGameplayBankV1 directory plus RAC1 Moby/TIE/shrub placements
├── RacLevelCoreIndexV1 ownership/linking for Moby/TIE/shrub model cores
├── exact 256-slot Ratchet sequence ownership and regular-frame parsing
├── compiler-side Ratchet frame-to-pose decoding with singular-scale preservation
├── bounded per-level tfrag/Moby/TIE/shrub texture and model asset loading
├── strict RAC1 world/hero collision parsing with exact packed provenance
├── versioned PreparedGameV2/LevelPackageV1 resource and mod-overlay containers
├── exact-Q6 CollisionWorldV1 compilation, canonical I/O, rebuilt grid, and bounded queries
├── neutral LevelBootstrapV1 spawn/death-plane compilation and binary I/O
├── neutral RenderSceneV1 textures/materials/meshes/instances and bounded binary I/O
├── neutral ActorLibraryV1 rigs/skinned models/materials plus canonical content digests
├── neutral ActorAnimationBankV1 clips, rig-digest binding, cadence, and canonical I/O
├── neutral EntitySceneV1 definitions and typed transform/render/actor/player bindings
├── neutral GameplaySceneV1 overlap collectibles and semantic inventory grants
├── neutral DestructibleSceneV1 health, hit volumes, damage channels, and item drops
├── bounded bind-pose palette construction and CPU linear-blend actor skinning
├── finite singular-palette position skinning for unlit animation consumers
├── fixed-cadence actor clip playback and deterministic locomotion selection
├── actor bind-pose baking into ordinary static RenderSceneV1 instances
├── compiler-only RAC collectible adaptation into render/entity/gameplay resources
├── compiler-only RAC Bolt-Crate adaptation into render/entity/destructible resources
├── compiler-only ordinary-Moby adaptation into shared actor/animation/entity resources
├── planet-agnostic eight-resource LevelPackageV1 compilation and cross-resource validation
├── hardened PreparedGameV2 filesystem loading and transactional publication
├── deterministic fixed-step/input replay boundary and planet-agnostic world/session
├── deterministic character controller, checkpoints, respawn, and player simulation
├── deterministic primary-combat timing plus damage/destruction/drop processing
├── deterministic entity materialization, interaction events, and persistent item totals
├── source-independent resolved-package foundation loader and player construction
├── bounded RacMobyClassV1 headers, packet ownership, and fixed asset ranges
├── regular Moby VIF/vertex-cache/strip/packet-local triangle recovery
├── regular high/low LOD assembly with cross-packet cache/material state
├── bounded RAC1 TIE high-LOD packet/strip/material/triangle recovery
├── shared PAL five-language subtitle-directory timing/text parsing
├── neutral SceneBlockDirectoryV1 parsing with owned blocks and 8-section layouts
├── bounded zero-copy SceneBlock VIF command parsing across sections 0-4
├── conservative SceneBlock VIF execution with VU1 memory/write provenance
├── neutral SceneBlock VIF/VU phases with inherited state and exact qword runs
├── exact SceneBlock task preamble/record execution with dual-bank frame input
├── stateful XGKICK GIF/GS primitive, raster, and texture-context snapshots
├── entry-16 source XYZ recovery with VIF/VU provenance and GS-color validation
├── profile-bound reusable ISO/ELF/SceneBlock compiler diagnostics and execution
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
├── TOC, WAD/bundle/companion/corpus/family/gameplay/level-core diagnostics
├── authoritative per-level collision/tree/surface diagnostics
├── single-level foundation package compilation and package-only movement smoke
├── all-level PreparedGameV2 foundation publication and published-root smoke
├── exact-profile all-level eight-resource native-game publication and validation
├── static high-LOD Moby and TIE placement/bounds diagnostics
├── scene-animation and subtitle diagnostics
├── scene-block/VIF/VU/GS, 2FIP, MapArt, PS2D, VAGp, and SBlk diagnostics
├── explicit-policy SBlk WAV and EE/R5900 boundary diagnostics
├── auto-fit wireframe TGA export from a selected real SceneBlock invocation
├── prepared-game extraction
└── ELF, DVP overlay/VU microprogram, and IOP/IRX inspection and diagnostics

openrc-launcher
├── image selection
├── disc inspection
├── asynchronous all-level native Prepare/Cancel and progress
├── persisted content-addressed PreparedGameV2 selection
├── exact compiler/profile validation and stale-cache rejection before Play
├── data-directory access
├── verified adjacent package-only runtime process launch
└── adjacent PE architecture and forbidden compiler-runtime import guard

openrc-runtime
├── prepared-root and level-only process contract
├── package-only PreparedGameV2 level/resource mounting with no ISO/ELF reader
├── fixed-step movement, collision, jump/reset, and third-person camera
├── semantic player-slot → entity → actor-model → rig resolution
├── semantic idle/walk/run selection with integer PAL-50-to-runtime-60 cadence
├── textured Ratchet high-LOD animated CPU skinning at the player transform
├── independent non-player pose/transform/visibility with per-model GPU sharing
├── authored entity/gameplay materialization and fixed-tick capsule/sphere collection
├── F/left-mouse primary action and neutral melee damage capsules
├── canonical damage/destruction/drop events and render-instance visibility
├── persistent semantic item totals
└── native D3D11 submission, auto-fit resize, and WARP fallback
```

## Portable executable boundary

Public LLVM-MinGW executables are linked with the compiler support runtime
statically. A post-link PE audit checks the expected architecture and rejects
the `libc++*`, `libunwind*`, `libgcc*`, `libstdc++*`, and `libwinpthread*`
runtime families. The static developer-build policy applies the same audit to
every test/developer executable. The portable publisher repeats the public-file
audit before and after copying each executable and compares its SHA-256, while
the Launcher independently validates the adjacent runtime before process
creation. This boundary applies to executable distribution; intermediate
developer build directories can still contain stale helpers and are not a
runnable package.

## Planned components

```text
compiler/
├── remaining interaction/camera resource schemas
├── deterministic remaining animation/interaction compilation for every supported level
└── optional cache migration beyond the current exact-profile rebuild path

tools/
├── remaining audio, texture, terrain, and TOC-table decoders
├── MIPS/R5900 analysis pipeline
├── symbol and type database
└── asset converters

runtime/
├── explicit ordered package-overlay selection
├── level-manager expansion and remaining gameplay entity behaviors
├── remaining actor state selection and animation blending
├── renderer fidelity and remaining specialized scene families
├── audio
├── game memory model
├── reconstructed game logic
└── save system
```

The package-based end-user boundary does not own an ISO parser. Raw disc, ELF,
VIF, VU, GIF, and GS formats terminate at the compiler boundary; package
loaders consume only versioned neutral OpenRC resources in normal world units.
The graphical `openrc-runtime` enforces that boundary as its only entrypoint:
it requires `--prepared-root` and `--level` and has no direct ISO/ELF mode. The
compiler emits exactly eight resources per level: `world/collision`,
`world/bootstrap`, `world/render-scene`, `actors/library`,
`actors/animations`, `world/entities`, `world/gameplay`, and
`world/destructibles`. The runtime mounts those neutral schemas, resolves
presentation and gameplay through semantic keys and authored IDs, and never
sees a RAC class ID, sequence slot, WAD offset, PS2 packet, source ISO, or boot
ELF. Source-backed recovery remains available only through compiler and
diagnostic libraries/tools.

The Launcher drives that shared all-level compiler, remembers the exact
content-addressed installation, validates its compiler ID, profile version,
manifest, package hashes, eight-resource shape, and cross-resource mount before
Play, and passes only the prepared root and level ID to the runtime. A cache
from an earlier profile is data from the same OpenRC installation, not a second
client: it is rejected as stale and replaced through the normal Prepare flow.
The eight-resource profile now has fresh real-data verification: all 19 levels
compile and validate, the matching cache is reused on a second preparation,
Veldin's two package-only gameplay smokes pass, and the D3D11 smoke proves a
mounted crate is drawn before attack and omitted after destruction. The same
neutral actor boundary now presents 16 class-749 instances from one shared
model/rig/texture set. Their proven source state-0 entry clip is identified by
the package's reserved `/initial/` semantic-key segment as a temporary
presentation fallback; runtime code contains no RAC class ID or sequence-slot
table. Per-instance behavior must replace it before this actor is exact.

This boundary is intentionally reusable beyond Veldin. Numeric IDs are scoped
to canonical tables, while cross-resource relationships use stable semantic
keys such as actor model, rig, and archetype identities. Later planets and
explicit mod overlays can therefore add or replace neutral assets without
teaching the runtime RAC1 serialization rules.

The current player presentation exercises that design with Ratchet's decoded
high-LOD textures, bind-space mesh, hierarchy, inverse binds, and skin weights.
The compiler decodes every regular Ratchet frame into neutral local joint
transforms, preserving authored zero scale rather than repairing it, then
packages every occupied per-level sequence under a numeric source-slot key
bound to the exact rig digest. Clean-room tracing of the player overlay now
identifies state 0 with slot 0 and ordinary grounded state 2 with slot 3/4
slow/full substates. The generic player uses
an integer cadence accumulator to advance PAL 50 Hz source updates on the 60 Hz
fixed simulation, interpolates and composes the palette, and sends it to the
renderer independently of the actor's world transform. A separate
position-only skinning path handles finite singular palettes for the current
unlit renderer. Moving clips switch only when actual horizontal pace crosses
the strict source thresholds `> 2.35` or `< 1.90`, and use the recovered
source-frame remap instead of restarting at frame zero. Airborne ticks hold the
last grounded palette until their source mapping is proven; no jump, fall,
crouch, landing, or wrench sequence is guessed. The Win32 adapter polls XInput
through runtime symbol
lookup, avoiding a new shipped DLL, and passes symmetric signed stick values
through unchanged except for canonicalizing the unmatched `-32768` endpoint.
That adapter intentionally owns no dead-zone or speed-response policy.
After the deterministic/replay boundary, the RAC-specific input layer samples
the signed device domain onto the original eight-bit DualShock lattice and
executes the recovered four-axis response (`center=127`, `dead-zone=48`,
`divisor=76`). A separate RAC player-locomotion layer then normalizes the
left-stick direction and applies the standard source pace policy: zero stays
zero, a non-zero radial magnitude below `0.82` targets `0.9`, and `0.82` or
above targets `5.7`. The core carries that target separately from direction,
so camera-relative rotation cannot quantize `0.9` into an approximation.
Every fixed tick also exposes the unquantized source magnitude and selected
pace for later source-state consumers. XInput therefore retains light-tilt
walking while keyboard movement acts as full travel, and headless/package-only
execution uses the same rule. Keeping pad sampling, source pace selection,
camera-relative rotation, and generic collision motion separate makes the
same recovered policy reusable on later planets. Exact state entry/exit,
pace smoothing, turning/collision modifiers, and state-specific movement
exceptions remain pending.
The generic interaction paths cover collectibles and a neutral
wrench-to-Bolt-Crate damage/destruction/drop loop. Finished attack and
destruction presentation, broader entity behaviors, enemies, menus, and the
original camera remain later runtime/compiler layers.

## Gameplay, combat, and destructible boundary

`GameplaySceneV1` is deliberately narrower than a reconstructed RAC behavior
system. It references `EntitySceneV1` authored IDs and stores a semantic item
key, local overlap-sphere center and radius, positive amount, and known flags.
The combined loader rejects a collectible whose entity definition or authored
transform is missing. At runtime, every entity definition receives a stable
`WorldV1` identity; each fixed tick tests the externally simulated player
capsule against enabled collectibles in canonical authored-ID order.

Inventory arithmetic is preflighted transactionally, so overflow cannot apply a
partial tick. The runtime then emits ordered events and destroys, disables, and
marks each matching entity collected exactly once. Inventory survives successful
scene reloads. The D3D11 frontend keeps static scene allocation immutable and
updates visibility through the existing EntitySceneV1 authored-ID to
RenderSceneV1 instance binding.

The current RAC1 source adapter stops at the compiler boundary. For the
supported profile it treats Moby class 13 as the Bolt collectible using
high-confidence community metadata, not a runtime class contract. Its model is
frozen in bind pose and baked into static render instances. The neutral item key
is `openrc.currency/bolts`; `amount = 1` is an explicit compiler policy rather
than a recovered per-placement value. See
[GameplaySceneV1](GAMEPLAY_SCENE_V1.md) for the complete contract and current
validation status.

`PlayerCombatV1` turns the neutral `primary_action` input edge into a bounded,
fixed-tick melee capsule using the post-movement player pose. The Windows
frontend maps both `F` and the left mouse button to that action. Damage carries
only a neutral channel, amount, source identity, and attack sequence; the
runtime does not branch on a wrench or RAC class number.

`DestructibleSceneV1` links that damage boundary to neutral entity IDs, health,
accepted channels, local hit spheres, and ordered semantic drops. The current
compiler-only RAC adapter maps static Moby class 500 to Bolt Crates, excludes
them from the flattened static-Moby pass, and emits one shared model plus
independent render/entity/destructible records for each placement. One health
and one Bolt drop are explicit OpenRC policy rather than claimed recovered
PVar behavior. Destroying a crate disables its render binding and grants the
drop transactionally. See [DestructibleSceneV1](DESTRUCTIBLE_SCENE_V1.md).

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
vertices and primitive emissions. A profile-bound owning loader keeps exact
ISO/ELF verification and execution on the compiler/diagnostic side of the
prepared-package boundary. For entry 16, a profile-bound recovery layer identifies the one V4-8
UNPACK whose output count matches the decoded GS submission count, then
requires its complete index-to-descriptor-W-to-signed-XYZ/adjacent-RGBA chain
to agree with every decoded GS color. The selected index address is therefore
derived per record instead of frozen to record 0's qword 250, and every hop
retains its last VIF write. Full-level mode independently starts every record
from the same validated entry-0/TOP=0 state, merges complete raster and source
triangle batches under aggregate limits, and reports diagnostic GS-only records
separately. The earlier developer viewer's orbit was isolated PC-side
diagnostic state: it never rewrote the VU frame transform or claimed to
reproduce the original camera. The public runtime no longer exposes that
source-backed entrypoint. Static Moby and TIE batches retain
their decoded material UVs and sample independent normalized texture banks
through explicitly bounded D3D regions with depth ordering. The GIF/GS layer
also retains both TEX0 and CLAMP contexts on every
vertex/primitive snapshot, even when PRMODE leaves the effective context
unresolved. The RAC1 SceneBlock adapter converts complete STQ to `S/Q,T/Q` and
accepts a table-index material only from the selected context or the sole
programmed context. Those tfrag batches use the same D3D texture path;
unresolved batches remain explicit wireframe fallbacks. Collision semantics,
metal and animated model paths, the live game camera, and gameplay remain
outside this layer.

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
Primary extents 1 and 2 of every level are now parsed as complete
`RacGameplayBankV1` records. Their 0x94-byte directory contains 37 little-endian
slots, with one reserved zero slot and 36 pointers to named gameplay sections;
0x0c zero bytes align the first block to 0xa0. Pointers are checked against the
RAC1 physical serialization order and exposed as disjoint borrowed ranges.
Format probing additionally requires the 0x50-byte level-settings block, a
bounded non-empty moby class list, and a moby instance block whose reserved
header words are zero and whose static records each declare the proven 0x78
byte RAC1 layout. The PVar pass proves one dense owner table shared by static
Mobies, cameras, and sound instances; derives aligned, contiguous opaque data
ranges; and validates the terminated Moby-link and same-PVar relative-pointer
fixup streams without assigning class-specific field meanings. Both regional
variants on all 19 levels pass, while their differences remain isolated to
opaque PVar data rather than being assigned an unproved timing meaning.
The raw level index in primary-extent-0 subrange 2 is independently parsed as
`RacLevelCoreIndexV1` and cross-checked against the encoded/decoded subrange-10
asset WAD. Its Moby class list must agree with the gameplay bank in exact count
and order. Local model offsets are converted into bounded ranges only from
proven neighboring asset boundaries; zero/external entries and shared gadget
WADs remain distinct ownership cases.
`RacMobyClassV1` then owns copied header, sequence, packet, skeleton, shadow,
and fixed-range metadata without retaining input pointers. The packet geometry
layer accepts only regular high/low packets and reconstructs VIF-delivered
texture coordinates, texture switches, delayed vertex-cache indices, strips,
and topology. Positions remain explicitly packet-local at this layer. The LOD
assembler executes packets in table order, carrying the 512-entry vertex cache
and current texture separately for high and low LOD; duplicate vertices retain
their transfer-local UV and exact source provenance. The level asset loader
links local/shared ownership back to gameplay placements. It also slices raw
GS RAM from primary-extent-0 subrange 3 and decodes every 0x10-byte tfrag and
Moby, TIE, and shrub texture record against the shared-texture block in
decompressed subrange 10.
Ordinary-Moby animation sequences reuse the proven regular pose record, but
their packed frame addresses are relative to the complete class asset rather
than the individual sequence. `parse_rac_moby_sequence_v1` normalizes that
addressing, and `compile_rac_moby_animation_bank_v1` emits neutral clips pinned
to the exact rig. Unclassified clips retain source-slot keys until executable
state dispatch proves their gameplay meaning.
Base pixels remain linear PSMT8 indices; the decoder swaps GS CLUT address bits
3/4, expands PS2 alpha, and owns both indexed and RGBA output. Each model keeps
its 16 local material slots, and the static scene builder retains UVs while
resolving every triangle to a contiguous global-texture batch or an explicit
untextured fallback. The compiler-side static builder applies
`T * S * Rz * Ry * Rx`. Its coordinate-domain policy is
explicit: native-package compilation and CLI diagnostics retain world units;
the former source-viewer adapter converted Moby output to 1,024 raw SceneBlock
ITOF0 units per world unit before its bounded diagnostic merge. Model loading
and preparation also have aggregate workspace
caps rather than multiplying per-model limits by the class count.
TIE classes use their own bounded high-LOD packet format. Their local positions
already include the class scale; the static builder applies the complete
column-major gameplay matrix, ignores its non-homogeneous final `0.01` word for
XYZ, performs no perspective divide, and then applies the same explicit 1,024
unit conversion. Local TIE materials resolve through each class's slot table
into a separate texture bank. Shrub class, placement, billboard, and texture
metadata are retained for the next specialized geometry decoder.
Metal and bangle geometry, mip/filter material state, skeletal bind/animation,
and the original visibility policy remain separate rather than being guessed.
MapArtV1 composes three palette-compatible images without assigning gameplay
meaning to the remaining opaque regions. The PS2D parser likewise retains
unknown header words and tagged payload keys rather than treating guesses as
format contracts.

The original extracted-file preparation is written into a unique staging
directory, hashed while it is streamed, described by a deterministic manifest,
and published with a no-replace directory rename. Existing preparations are
rehashed before reuse.

The separate PreparedGameV2 publisher accepts an explicit absolute root,
canonical manifest, and caller-owned level-package byte spans. It validates
every package identity, size, digest, relative path, and nested resource before
writing. A verified sibling staging tree is promoted with same-parent renames;
replacement retains and restores the previous destination on cancellation or
failure. It neither opens an ISO nor discovers overlay files. The current CLI
compiler supplies all 19 eight-resource level packages—collision, bootstrap,
render scene, actor library, actor animations, entity scene, gameplay scene,
and destructible scene—to this publisher. Actor/entity mounting is accepted
only as a complete pair. Animation banks, gameplay, and destructible scenes
remain optional only in the generic loader for older or independently authored
packages; the exact current native profile requires all three. The runtime
validates level identity plus all player-slot, actor-model, rig, animation-rig
digest/joint-count, render-instance, transform, collectible, and destructible
relationships before presenting content. Both the CLI and Launcher invoke the
same compiler service, and the graphical runtime mounts the published result
directly. The current eight-resource profile has fresh all-level
publication/reuse evidence plus package-only and graphical Veldin
attack/destruction smoke coverage.

## Configuration and generated data

On Windows:

- `%APPDATA%\PlunkDev\OpenRC` contains portable user preferences;
- `%LOCALAPPDATA%\PlunkDev\OpenRC` contains extracted data and machine-specific
  state;
- cache and logs are stored in `cache` and `logs` below the local directory.

The selected ISO path plus the exact prepared-root path and manifest digest are
stored in `%LOCALAPPDATA%\PlunkDev\OpenRC\launcher.ini`. On first load, an older
roaming `launcher.ini` is copied there automatically and retained as a backup;
the one-line settings format is migrated without losing the ISO selection.
Content-addressed native installations live below the local
`prepared-v2/openrc-rac-2002` directory. Game data must never be written to the
roaming configuration directory. The saved prepared root is a versioned cache,
not an installed copy of another client. On restore and before Play, the
Launcher checks it against the current exact compiler/profile contract; stale,
partial, or damaged data is disabled and rebuilt in place through Prepare
without discarding unrelated preferences.
