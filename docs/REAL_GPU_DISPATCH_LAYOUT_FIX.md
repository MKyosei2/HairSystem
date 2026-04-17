This patch fixes a real C++/HLSL constant-buffer layout mismatch in the DX11 viewer.

Problem:
- HairSim.hlsl declares these fields after SphereData/CapsuleData:
  - UseCompactedGuideDispatch
  - CompactedGuideCount
  - FollowerUpdateStride
  - FollowerUpdatePhase
  - UseCompactedFollowerDispatch
  - CompactedFollowerCount
- GpuHairViewer::SimConstants in C++ was missing the first two fields.
- That shifted every later value by 8 bytes on the GPU side.
- As a result, the shader could read UseCompactedGuideDispatch as 1 even though the CPU never intended to enable guide compaction.
- The simulation would then enter the compacted-guide path with no valid compacted-guide inputs, which can make hair appear for a frame and then disappear.

Fix:
- Add useCompactedGuideDispatch and compactedGuideCount to SimConstants in C++.
- Explicitly write both guide compaction fields as 0 every frame.
- Only enable compacted follower dispatch when all required GPU resources are actually present.
