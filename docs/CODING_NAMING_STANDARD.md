# Coding and Naming Standard

This repository uses a small set of naming rules so new work lands in a predictable shape.

## 1. File and folder names

### C++ source and headers
- Use **PascalCase** for `.cpp` and `.h` files.
- Keep the file name aligned with the main public type or feature in that file.
- Acronyms that represent a stable technical term stay uppercase.
  - `LOD.h`
  - `USDExport.cpp`
  - `DX11Viewer` only if the symbol is also named that way
- Examples:
  - `ReducedModel.cpp`
  - `GpuHairViewer.h`
  - `Main.cpp`

### Python scripts
- Use **snake_case.py**.
- Start helper scripts with the subsystem name when they belong to a pipeline.
  - `hairgpu_phase1_cli.py`
  - `naming_audit.py`

### Documentation
- Use **UPPER_SNAKE_CASE.md** for process, design, and stage notes.
- Keep abbreviations stable across docs.

## 2. C++ identifier naming

### Types
- `PascalCase`
- Applies to classes, structs, enums, and type aliases.
- Examples:
  - `HairAsset`
  - `ActiveSetPlan`
  - `GpuSimulationConfig`

### Functions
- `PascalCase` for public API functions.
- `PascalCase` for file-local helpers too, unless a third-party callback requires another style.
- Prefer descriptive verb phrases.
- Examples:
  - `BuildReducedHairAsset`
  - `LoadHairAssetFromAnySupportedInput`
  - `ParseUIntArgument`

### Variables and fields
- `camelCase`
- Examples:
  - `guideCount`
  - `frameTimeMs`
  - `selfCollisionRadius`

### Constants
- `kPascalCase` for file-local constants.
- Example:
  - `constexpr uint32_t kDefaultBucketCount = 32768u;`

### Enum values
- `PascalCase`
- Examples:
  - `Full`
  - `Reduced`
  - `Frozen`

### Namespaces
- lowercase only.
- Examples:
  - `hairgpu`
  - `hairgpu::viewer`

## 3. Prefixes and suffixes

- Use clear unit suffixes when values are unit-bound.
  - `frameTimeMs`
  - `elapsedSeconds`
  - `targetFps`
- Use role suffixes for container-like values when helpful.
  - `guideIndices`
  - `summaryRows`

## 4. Abbreviations

Allowed stable abbreviations:
- `GPU`
- `CPU`
- `LOD`
- `USD`
- `DX11`
- `CLI`
- `CSV`
- `JSON`

Do not introduce multiple spellings for the same term.
- Use `LOD`, not `Lod` in file names.
- Use `fps` only for compact variable names and UI labels, otherwise prefer `Fps` in PascalCase symbols.

## 5. Includes and paths

- Public includes use the installed style:
  - `#include "hairgpu/LOD.h"`
- Keep include path spelling identical to the actual file name.

## 6. New code checklist

Before committing:
1. File names follow the casing rules.
2. New public APIs use PascalCase.
3. Variables use camelCase.
4. New docs use the repository markdown naming rule.
5. Run `tools/dev/naming_audit.py`.
