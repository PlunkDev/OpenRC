# LevelBootstrapV1

`LevelBootstrapV1` is the small, platform-neutral resource that lets the
native runtime enter any prepared level without inspecting original PS2
records. In a `LevelPackageV1` it uses:

- resource ID `world/bootstrap`
- type ID `openrc.level-bootstrap`
- schema version `1`

The payload owns the level identity, an absolute world-space death height, an
explicit default spawn ID, and one or more named spawn transforms. It contains
no source-file offsets and no level-specific branches.

## Binary layout

All integers and IEEE-754 binary64 values are little-endian. Neutral world
coordinates are right-handed and Z-up; yaw is in radians about +Z.

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 8 | `ORLBOOT\0` magic |
| `0x08` | 4 | format version (`1`) |
| `0x0c` | 4 | header bytes (`64`) |
| `0x10` | 8 | exact total payload bytes |
| `0x18` | 4 | level ID |
| `0x1c` | 4 | default spawn ID |
| `0x20` | 8 | absolute death-height Z |
| `0x28` | 4 | spawn count |
| `0x2c` | 4 | spawn-record bytes (`40`) |
| `0x30` | 16 | reserved zero bytes |

Each 40-byte spawn record contains a 32-bit ID, four reserved zero bytes,
three binary64 feet-position components, and one binary64 facing yaw. Records
are strictly ascending by ID. The default ID must occur exactly once.

Writers reject non-finite values, sort records, reject duplicate IDs, and
canonicalize signed zero to positive zero. Readers require that canonical
representation, exact size, known version and record widths, zero reserved
bytes, and no trailing data. Both paths enforce caller-provided input and
spawn-count limits before allocation. Every player spawn must also lie
strictly above the level's absolute death height.

## RAC1 adapter

The clean-room `RacGameplayBankV1` adapter uses the typed death height and
requires exactly one static Moby whose class ID is zero. That Moby's typed
position and Z rotation become spawn `0`. Ship position and ship rotation are
unrelated metadata and are deliberately ignored.
