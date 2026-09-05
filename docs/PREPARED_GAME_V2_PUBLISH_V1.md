# PreparedGameV2 filesystem publication V1

`publish_prepared_game_v2_v1` is the neutral compiler-to-filesystem boundary
for a complete PreparedGameV2 base installation. It receives only:

- an explicit absolute destination root;
- one in-memory canonical `PreparedGameV2` manifest;
- exactly one caller-owned byte span for every referenced base
  `LevelPackageV1`;
- explicit parser and aggregate limits.

It never opens an ISO, searches extraction output, scans a mod directory, or
copies data that the caller did not supply. Optional overlay references remain
manifest metadata. Overlay manifests and overlay packages are installed by a
separate, explicit mod workflow.

## Validation before writes

The publisher serializes and parses the manifest through the canonical V2
codec. The in-memory level and overlay order must already match that canonical
form. Before it creates staging, it requires all of the following:

1. explicit package inputs and manifest level references form the same set of
   level IDs, with no missing, extra, empty, or duplicate input;
2. every byte span has the exact size and SHA-256 named by its reference;
3. every span is a canonical base `LevelPackageV1`;
4. package level ID, build ID, and content API version match the manifest;
5. referenced paths retain their portable lower-case relative meaning on the
   host filesystem;
6. no referenced file is another referenced file's parent, and no level
   package collides with `prepared-v2.orpg`.

Thus an incorrect compiler result cannot leave even a staging tree behind.

## Filesystem boundary

The destination must be absolute, non-root, and lexically free of `.` or `..`.
Every existing ancestor, and an existing destination itself, must be a plain
directory. Symbolic links, junctions, mount-style reparse points, ordinary
files, and paths outside the explicit parent are rejected.

Staging, backup, and failed-commit quarantine names are generated as siblings
of the destination. The implementation checks the parent relation before every
rename and only recursively removes publisher-owned sibling names beginning
with `.openrc-`. Newly written files use create-new/no-follow semantics and are
flushed before commit.

## Publication transaction

The package files are emitted in manifest level order into a new staging
directory. `prepared-v2.orpg` is emitted last, so a visible manifest never
describes a partially written staging tree. The hardened PreparedGameV2 reader
then reopens that manifest and every referenced package, repeating the exact
size, hash, nested payload, and identity checks.

Commit uses same-parent rename operations:

1. an existing destination is renamed to a unique backup sibling;
2. verified staging is renamed to the destination name;
3. the destination is verified again through the hardened reader;
4. only then is the old backup removed.

Each rename is an atomic filesystem operation and no partially populated tree
is promoted. Portable filesystems do not provide one universal atomic
directory-exchange primitive, so replacement uses two ordered renames. On a
normal error or caller cancellation between them, the old directory is renamed
back. If post-promotion verification fails, the new tree is moved to a
publisher-owned quarantine name, the old tree is restored, and the quarantine
is removed. Successful post-promotion verification is the transaction's commit
point. Backup cleanup happens afterward and cannot roll the verified new tree
back; a cleanup failure is reported while the new destination remains installed
and any surviving backup data is left available for recovery.

The optional cancellation callback has two safe checkpoints: after complete
staging verification and after the prior destination has been backed up. The
latter deliberately runs through the production rollback path.

## Determinism

Temporary sibling names are operational and intentionally unique. The durable
destination contents are deterministic: package files are the exact caller
bytes and the manifest is the canonical V2 encoding. Repeating publication
with identical inputs therefore produces byte-identical durable files. No
timestamps, host paths, staging names, or local configuration enter prepared
content.
