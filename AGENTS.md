# Repository Guidelines

## Project Structure & Module Organization

`src/` contains the C++17 application. Window orchestration lives in `src/app/`, Qt widgets in `src/ui/`, image loading/navigation/processing/analysis/cache code in `src/core/`, and small platform helpers in `src/util/`. `tests/ImageViewerCoreTests.cpp` is the standalone core regression executable. Translation sources are under `translations/`; design notes and handoff records are under `docs/`. `cmake/Config.props` supplies Visual Studio dependency paths, while `scripts/` contains repeatable build and validation helpers. Treat `bin/`, `obj/`, and `build/` as generated output.

## Build, Test, and Development Commands

Run commands from the repository root in PowerShell:

```powershell
.\scripts\build.ps1 -Cfg Debug
.\scripts\build.ps1 -Cfg Release
.\bin\Tests\Debug\ImageViewerCoreTests.exe
.\scripts\check-bom.ps1
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH="$env:QTDIR" -DOpenCV_DIR="$env:OPENCV_DIR"
cmake --build build --config Release
```

The build script compiles the VS2019 `x64` solution and deploys required Qt/OpenCV DLLs. Run the matching Debug and Release test executables before release. `check-bom.ps1` verifies source encoding. Launch locally with `bin\Debug\ImageViewer.exe [image-path]`.

## Coding Style & Naming Conventions

Use C++17, four-space indentation, braces on a new line, RAII, and explicit QObject ownership. Follow existing names: integer variables start with `n`, booleans with `b`, and types use PascalCase. Keep hot paths allocation-conscious and caches bounded. Use Qt APIs for portability before Windows-specific APIs. Never add `QStringLiteral`; construct strings with `QString(...)`. Wrap user-visible text in `tr()` and update `translations/ImageViewer_en_US.ts`. New C++ files require a short dated header describing function and purpose.

## Testing Guidelines

Tests use a custom executable rather than a unit-test framework. Add focused functions to `ImageViewerCoreTests.cpp`, label logs as `TEST-XX`, and state the business behavior being verified. Tests must return nonzero on failure. For UI changes, manually verify image drag/drop, thumbnail loading, zoom/pixel grid behavior, Chinese text, panel visibility, and responsiveness. Include Debug and Release results in the PR.

## Commit & Pull Request Guidelines

Follow the existing Conventional Commit pattern: `feat:`, `fix:`, `perf:`, `build:`, or `docs:` followed by a concise imperative summary. Keep commits scoped and stage only task-related files. PRs should explain the user-visible outcome, implementation risk, and validation performed; link relevant issues or design notes. Attach before/after screenshots for visual changes and call out dependency, translation, or performance impacts.

## Configuration Safety

Prefer `QTDIR` and `OPENCV_DIR` environment variables over committing machine-specific paths. Do not commit generated binaries, caches, logs, credentials, or local IDE state.
