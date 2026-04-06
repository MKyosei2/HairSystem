# Stage 10 notes

Implemented in this patch:

- Public `HairAlembicSettings` + `AlembicRoundTripReport` API in `AssetIO.h`
- `RoundTripHairAssetThroughAlembic(...)` helper for save/load/self-test validation
- Stronger Alembic CMake discovery in `cmake/FindAlembic.cmake`
- CLI command:
  - `alembic-roundtrip-selftest <input> <temp_output.abc> [report.txt]`
- Alembic scalp metadata round-trip support through user properties:
  - normals
  - uvs
  - skin bone indices
  - skin bone weights
  - boneCount

Verified in this environment:

- Linux/CMake build of `hairgpu_core` and `hairgpu_cli`
- `alembic-roundtrip-selftest` on a sample `.hairbin`
- Compatibility `.abc` export/load round-trip with report output

Important limitation:

- Real binary Alembic SDK execution is still environment-dependent.
  This patch wires the SDK path and adds self-test/reporting, but a true SDK-enabled run still requires
  `HAIRGPU_WITH_ALEMBIC=ON` plus an installed Alembic SDK and a matching toolchain.
- DX11 runtime / Windows GPU execution was not verified in this Linux environment.
