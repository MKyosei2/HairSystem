# GPU Broadphase Stage

## 追加したもの

- dense uniform grid broadphase を維持
- sparse な world-space motion に対応する **spatial hash broadphase** を追加
- **adaptive spatial hash** を追加
- **runtime sorted-grid broadphase (bitonic sort + cell start/end)** を追加
- CPU 参照では **radix sort + cell start/end** を追加
- hash bucket head + particle next による linked-list 構築
- SelfCollisionCS 内で dense / hash / legacy を切替
- CPU 参照実装 `broadphase-stats` を追加

## GPU 側の構造

### Dense grid
- `GridCellCounts`
- `GridCellIndices`
- 固定 bounds / fixed capacity

### Spatial hash
- `GridHashHeads`
- `GridParticleNext`
- signed cell coord を hash bucket に落とし込み
- bucket collision は実セル座標の再比較で除去

### Sorted grid
- `SortedKeys`（viewer 実装では `u4`）
- `SortedParticleIndices`（viewer 実装では `u5`）
- `SortedCellStarts` / `SortedCellEnds`（viewer 実装では `u6` / `u7`）
- build → bitonic sort → cell range 構築 → neighbor cell query

## viewer 操作

- `G`: GPU broadphase と legacy self collision の切替
- `B`: dense uniform grid / spatial hash / adaptive spatial hash / sorted grid の切替

## CLI

```text
hairgpu_cli broadphase-stats <input> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount]
hairgpu_cli broadphase-export <input> <output.csv> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount] [maxPairs]
```

出力される主な値:

- insertedParticles
- occupiedBins
- candidatePairs
- average / max bin occupancy
- spatial hash / adaptive hash 使用時の bucket occupancy
- sorted grid 使用時の radix-sorted cell start/end range
- same-strand window と same-root-triangle の strand-aware pair culling

## 今後の伸び代

- radix sort + prefix sum ベースへの置換
- multi-level voxel / adaptive cell size の sorted path 拡張
- runtime 側の broadphase debug draw と GPU readback の可視化強化
