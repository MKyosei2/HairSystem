# Build Fix 4 Patch

This patch replaces the remaining non-ASCII wide string literals in `solver_lab/src/Main.cpp` with ASCII-only messages.

Why:
- MSVC can emit C2001/C2143 around string literals when source encoding is misdetected.
- The previous file still had 3 Japanese wide strings near the failing lines.

Apply:
1. Extract this ZIP into your existing project folder.
2. Overwrite `solver_lab/src/Main.cpp`.
3. In Visual Studio, use **Release | x64**.
4. Run **Rebuild**.
