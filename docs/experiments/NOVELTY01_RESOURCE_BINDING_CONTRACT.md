# Novelty 01 GPU Resource / Binding Contract

> 自動生成元: `HairSegmentGroupAdaptivePayloadRuntime.hlsl`。正式なSRV/UAV契約を一覧化し、D3D11の8 UAV上限と同一ResourceのSRV/UAV同時Bind禁止を確認する。

## 共通規則

- `Novelty01Constants`は`b0`へBindする。
- 各Pass終了後にSRV/UAV/CB/CSをUnbindする。
- 同じD3D11 ResourceをSRVとUAVへ同時Bindしない。
- UAV registerは`u0`～`u7`の範囲に限定する。
- `DispatchIndirect`のArgument Bufferは`D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS`を持つ。

## Binding Mode一覧

### Mode 0

Entry Points: `N01InitializeCS`, `N01BuildBlocksCS`, `N01InvalidateBlocksCS`, `N01DescribeClassifyCS`, `N01FinalizeBlocksCS`, `N01ApplyTransitionsFinalFlagsCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01CurrentPositions` | `StructuredBuffer<float4>` |
| SRV | t1 | `N01PreviousPositions` | `StructuredBuffer<float4>` |
| SRV | t2 | `N01OldPayloads` | `StructuredBuffer<DynamicPayload>` |
| SRV | t8 | `N01CandidatePrefix` | `StructuredBuffer<uint4>` |
| SRV | t9 | `N01CandidateBlockOffsets` | `StructuredBuffer<uint4>` |
| UAV | u0 | `N01Cores` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u1 | `N01Extensions` | `RWStructuredBuffer<CoreExtension>` |
| UAV | u2 | `N01Caches` | `RWStructuredBuffer<StaticCache>` |
| UAV | u3 | `N01Descriptors` | `RWStructuredBuffer<ReuseDescriptor>` |
| UAV | u4 | `N01Blocks` | `RWStructuredBuffer<BlockDescriptor>` |
| UAV | u5 | `N01Flags` | `RWStructuredBuffer<uint4>` |
| UAV | u6 | `N01Counters` | `RWStructuredBuffer<uint>` |
| UAV | u7 | `N01ScratchCandidates` | `RWStructuredBuffer<SharedPointCandidate>` |

UAV数: **8** / 8

### Mode 1

Entry Points: `N01ScanLocalCS`, `N01ScanBlocksCS`, `N01ScatterListsCS`, `N01BuildSleepingIdentityListCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01FlagsIn` | `StructuredBuffer<uint4>` |
| SRV | t1 | `N01PrefixIn` | `StructuredBuffer<uint4>` |
| SRV | t2 | `N01BlockOffsetsIn` | `StructuredBuffer<uint4>` |
| UAV | u0 | `N01PrefixOut` | `RWStructuredBuffer<uint4>` |
| UAV | u1 | `N01BlockCounts` | `RWStructuredBuffer<uint4>` |
| UAV | u2 | `N01BlockOffsets` | `RWStructuredBuffer<uint4>` |
| UAV | u3 | `N01Counters` | `RWStructuredBuffer<uint>` |
| UAV | u4 | `N01DynamicList` | `RWStructuredBuffer<uint>` |
| UAV | u5 | `N01StaticList` | `RWStructuredBuffer<uint>` |
| UAV | u6 | `N01RepairList` | `RWStructuredBuffer<uint>` |
| UAV | u7 | `N01InvalidList` | `RWStructuredBuffer<uint>` |

UAV数: **8** / 8

### Mode 2

Entry Points: `N01CarryNonOptionalPayloadsCS`, `N01RemapPayloadCS`, `N01ApplyStaticCS`, `N01RepairCS`, `N01UpdateCacheCS`, `N01ShadowValidateCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01DynamicListIn` | `StructuredBuffer<uint>` |
| SRV | t1 | `N01StaticListIn` | `StructuredBuffer<uint>` |
| SRV | t2 | `N01RepairListIn` | `StructuredBuffer<uint>` |
| SRV | t4 | `N01RestPositions` | `StructuredBuffer<float4>` |
| SRV | t5 | `N01RestScalp` | `StructuredBuffer<float4>` |
| SRV | t6 | `N01CurrentScalp` | `StructuredBuffer<float4>` |
| SRV | t7 | `N01PreviousScalp` | `StructuredBuffer<float4>` |
| SRV | t8 | `N01ScalpTriangles` | `StructuredBuffer<TriangleData>` |
| SRV | t9 | `N01RootBindings` | `StructuredBuffer<RootBindingData>` |
| SRV | t10 | `N01OldPayloads` | `StructuredBuffer<DynamicPayload>` |
| SRV | t11 | `N01OldPayloadOwnersForRemap` | `StructuredBuffer<PayloadOwner>` |
| UAV | u0 | `N01Cores` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u1 | `N01Extensions` | `RWStructuredBuffer<CoreExtension>` |
| UAV | u2 | `N01Caches` | `RWStructuredBuffer<StaticCache>` |
| UAV | u3 | `N01Descriptors` | `RWStructuredBuffer<ReuseDescriptor>` |
| UAV | u4 | `N01NewPayloads` | `RWStructuredBuffer<DynamicPayload>` |
| UAV | u5 | `N01Positions` | `RWStructuredBuffer<float4>` |
| UAV | u6 | `N01PreviousPositions` | `RWStructuredBuffer<float4>` |
| UAV | u7 | `N01Counters` | `RWStructuredBuffer<uint>` |

UAV数: **8** / 8

### Mode 3

Entry Points: `N01ClearDynamicGuideLambdasCS`, `N01IntegrateDynamicGuidesCS`, `N01DistanceDynamicGuidesCS`, `N01BendDynamicGuidesCS`, `N01CollisionDynamicGuidesCS`, `N01StabilityGuardDynamicGuidesCS`, `N01IntegrateDynamicCS`, `N01DistanceConstraintCS`, `N01BendConstraintCS`, `N01CollisionCS`, `N01ClearHashCS`, `N01BuildHashCS`, `N01SelfCollisionCS`, `N01VelocityRecoveryCS`, `N01WatchdogCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01DynamicListIn` | `StructuredBuffer<uint>` |
| SRV | t1 | `N01GuideDirtyRangesIn` | `StructuredBuffer<GuideDirtyRange>` |
| SRV | t2 | `N01RestPositions` | `StructuredBuffer<float4>` |
| SRV | t3 | `N01SdfValues` | `StructuredBuffer<float>` |
| UAV | u0 | `N01Positions` | `RWStructuredBuffer<float4>` |
| UAV | u1 | `N01PreviousPositions` | `RWStructuredBuffer<float4>` |
| UAV | u2 | `N01Cores` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u3 | `N01Extensions` | `RWStructuredBuffer<CoreExtension>` |
| UAV | u4 | `N01Payloads` | `RWStructuredBuffer<DynamicPayload>` |
| UAV | u5 | `N01HashHeads` | `RWStructuredBuffer<uint>` |
| UAV | u6 | `N01ParticleNext` | `RWStructuredBuffer<uint>` |
| UAV | u7 | `N01Counters` | `RWStructuredBuffer<uint>` |

UAV数: **8** / 8

### Mode 4

Entry Points: `N01RootBindCS`, `N01FollowerLocalCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01FollowerList` | `StructuredBuffer<uint>` |
| SRV | t1 | `N01FollowData` | `StructuredBuffer<FollowData>` |
| SRV | t2 | `N01RestPositions` | `StructuredBuffer<float4>` |
| SRV | t3 | `N01CurrentScalp` | `StructuredBuffer<float4>` |
| SRV | t4 | `N01PreviousScalp` | `StructuredBuffer<float4>` |
| SRV | t5 | `N01ScalpTriangles` | `StructuredBuffer<TriangleData>` |
| SRV | t6 | `N01RootBindings` | `StructuredBuffer<RootBindingData>` |
| SRV | t7 | `N01FollowerDirtyRanges` | `StructuredBuffer<FollowerDirtyRange>` |
| UAV | u0 | `N01Positions` | `RWStructuredBuffer<float4>` |
| UAV | u1 | `N01PreviousPositions` | `RWStructuredBuffer<float4>` |
| UAV | u2 | `N01Counters` | `RWStructuredBuffer<uint>` |

UAV数: **3** / 8

### Mode 5

Entry Points: `N01BuildFollowerFlagsCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01CoresIn` | `StructuredBuffer<SegmentGroupCore>` |
| SRV | t1 | `N01FollowDataIn` | `StructuredBuffer<FollowData>` |
| UAV | u0 | `N01FollowerFlags` | `RWStructuredBuffer<uint4>` |
| UAV | u1 | `N01Counters` | `RWStructuredBuffer<uint>` |

UAV数: **2** / 8

### Mode 6

Entry Points: `N01BuildSharedCandidatesCS`, `N01ResolveSharedPointsCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01CoresIn` | `StructuredBuffer<SegmentGroupCore>` |
| SRV | t1 | `N01ExtensionsIn` | `StructuredBuffer<CoreExtension>` |
| SRV | t2 | `N01DescriptorsIn` | `StructuredBuffer<ReuseDescriptor>` |
| UAV | u0 | `N01Positions` | `RWStructuredBuffer<float4>` |
| UAV | u1 | `N01PreviousPositions` | `RWStructuredBuffer<float4>` |
| UAV | u2 | `N01Candidates` | `RWStructuredBuffer<SharedPointCandidate>` |
| UAV | u3 | `N01Counters` | `RWStructuredBuffer<uint>` |

UAV数: **4** / 8

### Mode 7

Entry Points: `N01ValidatePayloadOwnersCS`, `N01WritePayloadOwnersCS`, `N01RetirePayloadOwnersCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01LifecycleCoresIn` | `StructuredBuffer<SegmentGroupCore>` |
| SRV | t1 | `N01LifecycleExtensionsIn` | `StructuredBuffer<CoreExtension>` |
| SRV | t2 | `N01OldPayloadOwners` | `StructuredBuffer<PayloadOwner>` |
| SRV | t3 | `N01LifecycleDynamicList` | `StructuredBuffer<uint>` |
| SRV | t4 | `N01LifecycleCountersIn` | `StructuredBuffer<uint>` |
| UAV | u0 | `N01LifecycleCoresOut` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u1 | `N01LifecycleExtensionsOut` | `RWStructuredBuffer<CoreExtension>` |
| UAV | u2 | `N01NewPayloadOwners` | `RWStructuredBuffer<PayloadOwner>` |
| UAV | u3 | `N01LifecycleCounters` | `RWStructuredBuffer<uint>` |

UAV数: **4** / 8

### Mode 8

Entry Points: `N01ClearGuideDirtyRangesCS`, `N01BuildGuideDirtyRangesCS`, `N01PropagateFollowerRenderRangesCS`, `N01FinalizeGuideDirtyRangeCountersCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01RangeCores` | `StructuredBuffer<SegmentGroupCore>` |
| SRV | t1 | `N01RangeFollowData` | `StructuredBuffer<FollowData>` |
| SRV | t2 | `N01GuideRangesIn` | `StructuredBuffer<GuideDirtyRange>` |
| SRV | t3 | `N01RangeCountersIn` | `StructuredBuffer<uint>` |
| UAV | u0 | `N01GuideRangesOut` | `RWStructuredBuffer<GuideDirtyRange>` |
| UAV | u1 | `N01FollowerRangesOut` | `RWStructuredBuffer<FollowerDirtyRange>` |
| UAV | u2 | `N01RenderRangesOut` | `RWStructuredBuffer<RenderDirtyRange>` |
| UAV | u3 | `N01RangeFollowerFlags` | `RWStructuredBuffer<uint4>` |
| UAV | u4 | `N01RangeCounters` | `RWStructuredBuffer<uint>` |

UAV数: **5** / 8

### Mode 9

Entry Points: `N01CompareGroundTruthCS`, `N01VerifyInvariantsCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01ProposalPositions` | `StructuredBuffer<float4>` |
| SRV | t1 | `N01GroundTruthPositions` | `StructuredBuffer<float4>` |
| SRV | t2 | `N01FormalRestPositions` | `StructuredBuffer<float4>` |
| SRV | t4 | `N01FormalExtensions` | `StructuredBuffer<CoreExtension>` |
| SRV | t5 | `N01FormalOwners` | `StructuredBuffer<PayloadOwner>` |
| UAV | u0 | `N01GroundTruthMetrics` | `RWStructuredBuffer<GroundTruthMetric>` |
| UAV | u1 | `N01FormalCoresOut` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u2 | `N01FormalCounters` | `RWStructuredBuffer<uint>` |

UAV数: **3** / 8

### Mode 10

Entry Points: `N01BuildIndirectArgumentsCS`, `N01MigrateCoreBuffersCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01ArgumentCounters` | `StructuredBuffer<uint>` |
| SRV | t1 | `N01MigrationDynamicList` | `StructuredBuffer<uint>` |
| SRV | t2 | `N01MigrationStaticList` | `StructuredBuffer<uint>` |
| SRV | t3 | `N01MigrationCores` | `StructuredBuffer<SegmentGroupCore>` |
| UAV | u1 | `N01MigratingDynamicCores` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u2 | `N01MigratingStaticCores` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u3 | `N01ArgumentCountersOut` | `RWStructuredBuffer<uint>` |

UAV数: **3** / 8

### Mode 11

Entry Points: `N01ClearAtomicCompactionCountersCS`, `N01AtomicCompactCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01AtomicFlags` | `StructuredBuffer<uint4>` |
| UAV | u0 | `N01AtomicCounters` | `RWStructuredBuffer<uint>` |
| UAV | u1 | `N01AtomicDynamicList` | `RWStructuredBuffer<uint>` |
| UAV | u2 | `N01AtomicStaticList` | `RWStructuredBuffer<uint>` |
| UAV | u3 | `N01AtomicRepairList` | `RWStructuredBuffer<uint>` |
| UAV | u4 | `N01AtomicInvalidList` | `RWStructuredBuffer<uint>` |

UAV数: **5** / 8

### Mode 12

Entry Points: `N01InjectDesignScenarioCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| UAV | u0 | `N01ScenarioPositions` | `RWStructuredBuffer<float4>` |
| UAV | u1 | `N01ScenarioPreviousPositions` | `RWStructuredBuffer<float4>` |
| UAV | u2 | `N01ScenarioCores` | `RWStructuredBuffer<SegmentGroupCore>` |
| UAV | u3 | `N01ScenarioExtensions` | `RWStructuredBuffer<CoreExtension>` |
| UAV | u4 | `N01ScenarioDescriptors` | `RWStructuredBuffer<ReuseDescriptor>` |
| UAV | u5 | `N01ScenarioCounters` | `RWStructuredBuffer<uint>` |

UAV数: **6** / 8

### Mode 13

Entry Points: `N01InitializeRenderCacheCS`, `N01UpdateGuideRenderCacheCS`, `N01UpdateFollowerRenderCacheCS`

| 種別 | Slot | Resource | 型 |
|---|---:|---|---|
| SRV | t0 | `N01RenderSourcePositions` | `StructuredBuffer<float4>` |
| SRV | t1 | `N01RenderGuideRanges` | `StructuredBuffer<GuideDirtyRange>` |
| SRV | t2 | `N01RenderFollowerRanges` | `StructuredBuffer<FollowerDirtyRange>` |
| SRV | t3 | `N01RenderActiveFollowerList` | `StructuredBuffer<uint>` |
| SRV | t4 | `N01RenderCountersIn` | `StructuredBuffer<uint>` |
| UAV | u0 | `N01RenderPositionCache` | `RWStructuredBuffer<float4>` |
| UAV | u1 | `N01RenderCountersOut` | `RWStructuredBuffer<uint>` |

UAV数: **2** / 8

## Buffer Layout規則

- `SegmentGroupCore`、`CoreExtension`、`StaticCache`、`DynamicPayload`、`ReuseDescriptor`、`BlockDescriptor`はC++側`static_assert`とHLSL側宣言の順序を一致させる。
- `RuntimeConstants`と`Novelty01Constants`はフィールド順を完全一致させる。
- Counterは`0..63`の連続Indexを使用する。
- Payloadは4世代Ping-PongとOwner Tableを同一世代番号で管理する。
