# HairSystem.exe may be blocked by Windows policy
If Visual Studio shows application control policy blocked this file, the executable did not start.
In that state, Step0 real measurement is impossible because no frame is rendered and no GPU timing is collected.
Valid Step0 output requires RESULT_REAL.md and RESULT_REAL.csv. This build-only folder is not a real measurement.
Allow this executable path, then launch HairSystem.exe normally: C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\HairSystem.exe
Expected real-output files after successful launch: RESULT_REAL.md, RESULT_REAL.csv, and preset *_frames.csv files with frame rows.
