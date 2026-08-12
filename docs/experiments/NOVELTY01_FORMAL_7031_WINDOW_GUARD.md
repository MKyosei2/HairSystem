# Novelty01 Formal Revision 70.31 — Fixed Client Window Guard

70.31 preserves the 70.30 paired thesis design: 90 formal runs, 45 B0 and 45 P, with Smoke excluded from formal evidence and Qualification detached from formal evidence.

## Reason for 70.31

A real 70.30 formal session reached Soak after completing Pilot, Quality, Performance, and Determinism, then the native Viewer terminated with exit code 92. The Viewer intentionally uses exit code 92 when its render client area changes during formal measurement. This is an integrity failure, not a performance-threshold outcome, and must remain fatal.

## Guard policy

Every `HairSystem.exe --formal-run` process is attached to `fixed-client-window-guard-v1`. The guard waits for the already-established 1920x1080 client area, removes resize-frame and maximize affordances while preserving that client size, and monitors the client area for the Viewer lifetime.

The guard never repairs an actual resize after it occurs. Native Viewer exit code 92 is returned unchanged. If the Viewer itself returns success but the guard could not be attached, could not lock the required client area, or observed a client/display-size violation, the harness fails closed with exit code 96.

Each formal case writes `formal_window_guard_7031.json`. Report and FinalGate require exactly 90 guard records and every record must be `PASS` with no violations.

## Unchanged result policy

Performance, quality, measured thermal/clock threshold misses, and determinism threshold outcomes remain reportable experimental results rather than formal-session abort conditions. NaN/Inf, abnormal Viewer termination, input mismatch, frozen-input/evidence corruption, and missing required guard evidence remain fatal.
