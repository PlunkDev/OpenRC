# OpenRC workspace rules

- Build every Windows executable, including temporary diagnostics and probes,
  through a CMake target. Do not compile or launch ad-hoc MinGW executables.
- Keep `OPENRC_STATIC_MINGW_RUNTIME` enabled. Any executable that is launched
  must pass the repository PE import audit and must not depend on `libc++.dll`,
  `libunwind.dll`, `libgcc*.dll`, `libstdc++*.dll`, or `libwinpthread*.dll`.
- Use `scripts/build-portable.ps1` for the user-facing package. Only launch the
  verified executables published under `build-portable`.
- Keep original game images, extracted assets, and analysis output under the
  ignored `local` directory. Never commit copyrighted source data.
- Runtime code consumes neutral prepared packages only. RAC/PS2 decoders stay
  on the compiler side of the package boundary.
- Keep `README.md` a short overview: update its status table only when an area
  changes state. Record new capabilities and verified results in
  `docs/COMPONENTS.md` and new or changed `openrc-cli` commands in `docs/CLI.md`.
- On Windows, store roaming configuration under
  `%APPDATA%\PlunkDev\OpenRC` and local cache, logs, and machine-specific state
  under `%LOCALAPPDATA%\PlunkDev\OpenRC`. Migrate older locations without
  losing user settings.
