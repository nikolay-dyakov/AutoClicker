# Repository instructions

These instructions apply to the entire repository.

## Project constraints

- AutoClicker is a native Windows desktop application written in C++20 with the Win32 API.
- Keep the application compact and self-contained. Do not introduce Qt, another GUI framework, a
  package manager, or runtime DLL dependencies without an explicit project decision.
- Preserve Unicode builds and use the wide Win32 APIs.
- Keep the classic Windows visual style, compact layout, keyboard navigation, and DPI scaling.
- Preserve the existing `F6` start, `F7` stop, and `F8` toggle hotkeys unless a task explicitly
  changes them.
- Position picking must wait for the next left click, suppress that selection click, and allow
  right-click or `F7` cancellation.
- The live click count represents successful `SendInput` submissions; do not present it as proof
  that another application processed every click.
- Maintain compatibility with existing settings under `HKEY_CURRENT_USER\Software\AutoClicker`.

## Important files

- `main.cpp` contains the application, UI, timer, input, hotkey, picker, and settings logic.
- `CMakeLists.txt` defines the supported MSVC and MinGW builds.
- `version.h.in` generates the application version strings from the CMake project version.
- `app.rc`, `resource.h`, and `assets/autoclicker.ico` define Windows resources and the app icon.
- `docs/screenshot.png` is the README screenshot and should be refreshed after visible UI changes.
- `release/` is an ignored local staging directory. Do not commit or remove it unless requested.

## Build and verification

Use an out-of-source build. For MSVC:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

For MinGW-w64 with Ninja:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Before finishing a code change:

1. Build the Release configuration with an available supported toolchain.
2. Treat new compiler warnings as regressions.
3. For UI changes, verify the initial window, enabled and disabled states, keyboard focus, and DPI
   scaling. Update the screenshot when the visible layout changes.
4. For timer or input changes, verify start, stop, repeat limits, both mouse buttons, current and
   fixed positions, position-picker cancellation, and live statistics. Begin manual checks at a low
   CPS and use `F7` as the emergency stop.
5. Do not automate live input against unrelated applications or user data.

There is currently no automated test suite, so document the build and relevant manual checks in the
change summary.

## Change discipline

- Prefer focused changes and preserve unrelated user work.
- Use RAII or an equally clear ownership strategy for Win32 handles and restore timer resolution,
  hooks, hotkeys, and threads on every exit path.
- Keep hot paths allocation-free where practical and avoid blocking the UI thread.
- Update `README.md` when controls, hotkeys, build steps, or user-visible behavior changes.
- Keep GitHub Actions permissions minimal. Pin third-party actions to full commit SHAs with a nearby
  release-version comment, and update both only after verifying the release in its official
  repository.
- Keep ordinary CI read-only and free of downloadable executables. Releases are published only from
  `v*` tags by `.github/workflows/release.yml` and must include the executable and SHA-256 checksum.
- Do not add third-party actions to the write-capable release job. Keep the GitHub token scoped to
  the final publishing step, and preserve the protected `release` environment gate.
