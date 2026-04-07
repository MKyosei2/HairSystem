This patch fixes a real GPU-side constant-buffer layout bug and disables guide/follower compaction for stability.

Changes:
- SimConstants in C++ now matches HairSim.hlsl by including UseCompactedGuideDispatch and CompactedGuideCount.
- GPU title string changed to [GPU_LAYOUT_FIX_V3_NO_COMPACTION] so you can verify the new executable is running.
- Guide compaction is disabled explicitly.
- Follower compaction/indirect dispatch is disabled explicitly; followers use direct GPU FollowCS dispatch.
- HairSim.hlsl uses CompactedGuideCount / CompactedFollowerCount constants instead of reading unbound counter SRVs.
