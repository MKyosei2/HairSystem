# Step0 and Windows application control block

## What this package can and cannot do

Step0 means real measurement. It is complete only when the viewer executable runs, records frames, and writes:

- `RESULT_REAL.md`
- `RESULT_REAL.csv`
- non-empty `*_frames.csv` files under each preset folder

If Windows blocks `HairSystem.exe` before it starts, no C++ code in the project can measure FPS, GPU time, readback cost, self-collision cost, solver cost, or render cost. In that case the only correct result is:

- `EXE_BLOCKED_BY_WINDOWS_POLICY.md`
- build-only `RESULT.md`
- placeholder CSV files

Those files are not Step0 real measurements.

## Current observed blocker

Visual Studio shows:

`アプリケーション制御ポリシーによってこのファイルがブロックされました。`

This means the executable is denied by Windows Application Control / WDAC / AppLocker before the viewer code runs.

## Required action

Ask the PC administrator to allow the generated executable path, or run the project on a PC where locally built executables are allowed.

Typical paths:

- `C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\HairSystem.exe`
- `C:\Users\kyose\source\repos\HairSystem\bin\Release\HairSystem\HairSystem.exe`

After the EXE is allowed, build the project and launch `HairSystem.exe`. The viewer will autostart Step0 real measurement and write `RESULT_REAL.md`.

## Do not judge Step0 from RESULT.md

`RESULT.md` is only a preflight/build file. It intentionally exists even when Windows blocks the executable.

The only valid Step0 result is `RESULT_REAL.md`.
