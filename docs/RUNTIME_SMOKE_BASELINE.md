# Runtime smoke baseline (v14)

Reproduces the v14 checkpoint (`0.1.0-native-eight-resource-v14-level-installation`)
from a game image and records the results measured on the maintainer machine on
2026-10-06. Everything under `local/` is ignored by git; nothing here may be
committed from there.

All commands run in Windows PowerShell 5.1. Quote every path: the repository
path contains spaces and `!`. `<repo>` below is `D:\! Projekty\OpenRC`.
Use absolute paths everywhere; a relative `--prepared-root` aborts the run before
playback.

## Inputs and verified executables

| Item | SHA-256 |
| --- | --- |
| `<repo>\build-portable\openrc-runtime.exe` | `b2030594a42237e826f3748f327601d478a583219feb7d8fbb5fa94454618fd8` |
| `<repo>\build-portable\openrc-cli.exe` | `b3e9017c993c0cdc70e7fdb6e6e37c6c6d4af21d5742d9efc03f462f03b622f6` |
| `<repo>\local\ratchet-and-clank.iso` (PAL SCES-50916) | `0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260` |
| prepared boot ELF `SCES_509.16` | `17f8a846329fd10798cb97847e9610e5eecd1867668420ed627b2292a96b122b` |

## Commands for a fresh machine

```powershell
$repo = 'D:\! Projekty\OpenRC'
$bp   = "$repo\build-portable"
$L    = "$repo\local"
$F    = "$L\forensics\baseline-v14"
New-Item -ItemType Directory -Force $F | Out-Null

# 1. PE import audit (exit code 0 expected, no output)
& "$bp\openrc-portable-executable-audit.exe" "$bp\openrc-cli.exe"
& "$bp\openrc-portable-executable-audit.exe" "$bp\openrc-runtime.exe"

# 2. Stage 1: extract the boot ELF (about 20 s)
Get-FileHash "$L\ratchet-and-clank.iso"
& "$bp\openrc-cli.exe" prepare "$L\ratchet-and-clank.iso" "$L\prepared-stage1"
$elf = "$L\prepared-stage1\SCES-50916\0f18a6c84cd8d727ec8c21000a236ed5ce2f279cbb8d0682fa9747199ef73260\files\SCES_509.16"
Get-FileHash $elf

# 3. Compile all 19 levels (about 13 min) and validate (about 70 s)
& "$bp\openrc-cli.exe" prepare-native-game "$L\ratchet-and-clank.iso" $elf "$L\prepared-milestone1-v14"
& "$bp\openrc-cli.exe" validate-native-game "$L\prepared-milestone1-v14"

# 4 and 5. Smoke runs. Audio variables are set only for the child process
# (restored in finally); never persist them in User/Machine scope.
$env:OPENRC_AUDIO_DEVICE_NAME = 'Słuchawki (Oculus Virtual Audio'
$env:OPENRC_AUDIO_DEVICE_REQUIRED = '1'
try {
  $root = "`"$L\prepared-milestone1-v14`""
  $runs = @{
    'new-game-sequence' = @('--smoke-stage', 'new-game-sequence')
    'level0'            = @('--level', '0')
  }
  foreach ($name in $runs.Keys) {
    $args = @('--prepared-root', $root) + $runs[$name] + @('--smoke-test')
    $p = Start-Process "$bp\openrc-runtime.exe" -ArgumentList $args -WindowStyle Hidden -PassThru `
      -RedirectStandardOutput "$F\smoke-$name.stdout.log" -RedirectStandardError "$F\smoke-$name.stderr.log"
    $null = $p.Handle      # keeps the handle so ExitCode is readable
    $p.WaitForExit()
    "$name exit $($p.ExitCode)"
  }
} finally {
  Remove-Item Env:OPENRC_AUDIO_DEVICE_NAME, Env:OPENRC_AUDIO_DEVICE_REQUIRED
}
```

Notes:

- `WaitForExit(timeout)` followed by `ExitCode` returned an empty value in one
  attempt; reading `$p.Handle` first and using the parameterless `WaitForExit()`
  returns the real exit code.
- If no output device whose name starts with the configured text exists, do not
  fall back to the default output; record that as the result.
- Do not run `scripts/build-portable.ps1` while these executables are running.

## Expected log lines

`validate-native-game`:

```
OpenRC native game matches the current exact profile
Levels:                 19
Compiler profile:       0.1.0-native-eight-resource-v14-level-installation
Manifest SHA-256:       d9249c74834a1423cb6b3bdfdb5b5812991cc634521ce54b73323dc583237fd9
```

`--smoke-stage new-game-sequence` (stdout, exit 0):

```
OpenRC level state installation: level=0 writes=3457 revision_before=1194 revision_after=1195 completed_content_taken=1 canonical_session_retained=1 frozen_frame_preserved=1 level_state_installed=1 level_admitted=0 world_entered=0 level_playable=0
OpenRC normal New Game sequence: prepare_completed=1 cards=3 movies=3 fades=7 level_load_completed=1 transition_cleanup_completed=1 normal_sequence_barriers_executed=1 level_state_installed=1 next_owner=level/enter level_admitted=0 world_entered=0 level_playable=0
```

The sequence intentionally stops at the unimplemented `level/enter` barrier
(`world_entered=0`). Movies decode 616 / 1348 / 415 frames.

## Results on this machine (2026-10-06)

| Step | Result |
| --- | --- |
| PE audit `openrc-cli.exe`, `openrc-runtime.exe` | exit 0 both |
| ISO SHA-256 | `0f18a6c8…3260`, matches |
| `prepare` | exit 0, 19.9 s; ELF `SCES_509.16` SHA-256 `17f8a846…122b`, matches |
| `prepare-native-game` | exit 0, 762.4 s, 19 levels, 1631987716 level package bytes |
| `validate-native-game` | exit 0, 69.2 s, profile `0.1.0-native-eight-resource-v14-level-installation`, 19 levels |
| Manifest `prepared-v2.orpg` SHA-256 | `d9249c74834a1423cb6b3bdfdb5b5812991cc634521ce54b73323dc583237fd9` (HANDOFF: `d9249c74…`, matches) |
| `shared.orlevel` SHA-256 | `950a7c6488402e082a7b537033319c848ba8b22b878e1c1e3008fa696091bb01`, 223507737 bytes (HANDOFF: `950a7c64…`, matches) |
| Smoke `new-game-sequence` | exit 0, 151.4 s; `cards=3 movies=3 fades=7`, `writes=3457 revision_before=1194 revision_after=1195`, `next_owner=level/enter`, `world_entered=0` |
| Smoke `--level 0 --smoke-test` | exit 0, 4.4 s; stdout and stderr both empty (0 bytes), so there are no summary lines |
| Audio device | Oculus Virtual Audio present; runtime stderr reports `PCM audio output index=1 name=Słuchawki (Oculus Virtual Audio` (no fallback to default) |

The "58 shared resources" count is not printed by `validate-native-game`; it was
not independently confirmed in these logs (the validated profile string and the
shared hash match). The first smoke attempt (exit code not captured because of the
`WaitForExit(timeout)` issue above) produced identical log lines; the recorded
logs come from the second run.

Logs: `local\forensics\baseline-v14\` (`prepare.log`, `prepare-native-game.log`,
`validate-native-game.log`, `smoke-new-game-sequence.{stdout,stderr}.log`,
`smoke-level0.{stdout,stderr}.log`).

## Discrepancies against HANDOFF

None found in hashes, profile, level count or the `3457` / `1194→1195` / `3/3/7`
values. Differences of method only: the worktree `local` is a junction to
`D:\! Projekty\OpenRC\local`, and `prepare` refuses a root that passes through a
link, so commands must use the real `D:\! Projekty\OpenRC\local` path, not the
worktree path.
