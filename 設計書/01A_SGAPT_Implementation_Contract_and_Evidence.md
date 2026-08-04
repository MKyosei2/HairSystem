# SG-APT 実装契約・証拠対応表

## 1. 固定識別情報

- Repository: `MKyosei2/HairSystem`
- Branch: `Novelty01`
- 修復基準Commit: `4f4c6210d7be40a0b5d522789d2684ade86d3de9`
- 提案: GPU-Resident Segment-Group Adaptive Payload Type（SG-APT）

## 2. 正式Source of Truth

| 契約 | 正式ファイル |
|---|---|
| C++型・Variant契約 | `include/hairgpu/SegmentGroupAdaptivePayload.h` |
| SG-APT GPU処理 | `apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl` |
| Runtime/Dispatch | `apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl` |
| Ground Truth/Telemetry | `apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl` |
| Render Dirty Consumer | `apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl` |
| Formal Contract | `docs/experiments/NOVELTY01_FORMAL_CONTRACT.json` |
| Frozen Parameters | `docs/sample_assets/novelty01_frozen_parameters.json` |
| Canonical Scenarios | `benchmarks/novelty01/sgapt_scenarios/S01.json`～`S18.json` |
| Matrix/Gate/Report | `tools/final_stage/hairgpu_novelty01_*.py` |

## 3. 必須型不変条件

1. Segment Groupの固定IndexはTopologyが不変な間変化しない。
2. Static状態ではOptional Dynamic Payloadを所有しない。
3. Dynamic/Repair/ForcedDynamicでPayloadを参照する場合、Owner、Version、Generationが一致する。
4. Retire済みPayloadはSafe Reuse Epoch前に再利用しない。
5. Shared Pointは候補生成後、安定優先順位で単一Writerが確定する。
6. Version不一致、非有限値、Pool不足、Descriptor違反は黙って継続せずBaseline FallbackまたはSafe Stopへ移る。
7. Follower/Render Dirty RangeはDependency Versionと整合し、範囲外Dispatchを行わない。

## 4. Baseline独立性

- B0: MainのLegacy Full Dynamic。
- B1: SG-APT Core、Payload Pool、Owner Table、Descriptor、Compactionを使用しない独立Full Dynamic。
- B8: 一般的GPU最適化は許可するが、Fixed Core＋Optional Payload Lifetimeを使用しない独立Strongest Practical。
- B2～B7: PのControlled Route上でFeature Maskを固定し、各Baselineの除外要素を検証する。
- A1～A15: 除去対象Featureが実行時Feature Maskに残っていないことを検証する。

## 5. 証拠段階

| 段階 | 証明すること | 証明しないこと |
|---|---|---|
| Source/Contract | 型、Entry Point、経路、固定条件の存在 | 実GPUの正しさ・性能 |
| Build/Shader | Release x64と全HLSL Entry PointがCompile | 長時間安定・60 FPS |
| Readiness | P/S01、100k、600測定フレーム、出力経路 | 論文性能主張 |
| Formal Matrix | B0～B8/P/Ablationの比較可能データ | 統計的優位性 |
| Statistical Gate | 事前定義Gateと信頼区間を満たす | 未測定GPUへの一般化 |

## 6. Main統合禁止条件

- `files/`、`overlay/`、`replacement/`、旧Apply/Patcher/Verifierが残る。
- `GpuHairViewer.cpp`に`#define private`が残る。
- Root Workflowが存在しない、またはRuntime Patcherを実行する。
- Novelty01 ReadinessがBranch条件の矛盾で起動できない。
- Frozen AssetのManifest Version、Branch、groupsPerBlockが一致しない。
- B1/B8がSG-APT表現資源をBindする。
- Release x64、FXC、Native/Shader Self-Test、P/S01 Readinessのいずれかが未PASS。
