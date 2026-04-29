# 新規性以外の実装基盤：実装先ファイル対応設計書

## 0.1 実装先ファイル早見表

この設計書で実装する対象は、**新規性5要素そのものではなく、650パターン自動実験・telemetry・集計・baseline/variant管理のための周辺基盤**である。  
実装時に迷わないように、最初に「どの処理をどのファイルへ実装するか」を固定する。

### 0.1.1 追加する新規ファイル

| 種別 | ファイル | 役割 | 必須度 |
|---|---|---|---|
| Python新規 | `tools/final_stage/generate_experiment_650.py` | 32通りのfeature combination、10 scenario、2 seed、baseline 10件を生成し、合計650 caseのmanifestを出力する | 必須 |
| 出力ファイル | `results/exp_650/experiments_650.jsonl` | 1行1実験caseのmanifest。自動生成物なのでGit管理は任意 | 必須 |
| 出力ファイル | `results/exp_650/runtime_suite_650.json` | 既存 `hairgpu_runtime_suite.py` が読みやすいsuite形式。自動生成物 | 必須 |
| 出力ファイル | `results/exp_650/cases/<case_id>/settings.json` | caseごとの実行設定保存 | 必須 |
| 出力ファイル | `results/exp_650/cases/<case_id>/error.json` | 失敗caseの内容保存。成功時はなくてもよい | 必須 |

`generate_experiment_650.py` 以外は実行時の生成物であり、コードとして直接編集する対象ではない。

---

### 0.1.2 修正する既存C++ファイル

| ファイル | 実装する内容 | 触る主な既存箇所 | 備考 |
|---|---|---|---|
| `apps/dx11_viewer/src/GpuHairViewer.h` | 実験case設定struct、PerformanceSample拡張、telemetry出力関数宣言、frame counter用member追加 | `struct PerformanceSample`、`m_performanceSamples` 周辺、private member/function宣言部 | C++側の中心。まずここから差分を入れる |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | 環境変数読み込み、warmup/measurement分離、auto-exit、CSV拡張、JSONL追加、summary.json拡張 | `GpuHairViewer::Run()`、`ExportPerformanceCsv()`、`ExportRuntimeReport()`、`HAIRGPU_AUTO_TELEMETRY` 処理周辺 | C++側で最も大きく変更するファイル |
| `apps/dx11_viewer/src/AppMain.cpp` | 原則変更しない。必要な場合のみCLI引数を環境変数へ変換する薄い処理を追加 | `WinMain` または main相当 | 基本は環境変数方式なので変更不要 |
| `apps/dx11_viewer/CMakeLists.txt` | 原則変更しない。新しいC++ファイルを分けた場合だけ追加 | source list | 今回は既存 `GpuHairViewer.*` 内に実装する方針なので基本不要 |

#### C++側で実装してはいけない場所

| ファイル | 理由 |
|---|---|
| `apps/dx11_viewer/shaders/*.hlsl` | 今回は新規性solverやGPU passの実装ではないため触らない |
| `src/hairgpu_core/*.cpp` | 650実験のtelemetry制御が目的なので、core solverは原則変更しない |
| `apps/hairgpu_cli/src/main.cpp` | viewer telemetryを650実験の対象にするため、まずは変更対象外 |

---

### 0.1.3 修正する既存Pythonファイル

| ファイル | 実装する内容 | 触る主な既存関数 | 必須度 |
|---|---|---|---|
| `tools/final_stage/hairgpu_final_run.py` | viewer起動時にcase情報を環境変数として渡す。frames.jsonlも待機対象に追加する。単発case実行CLIを拡張する | `run_viewer()`、`main()`、`argparse` 定義 | 必須 |
| `tools/final_stage/hairgpu_runtime_suite.py` | `runtime_suite_650.json` を読み、650 caseを順番に実行する。resume、fail-policy、case-filter、variant-filter、scenario-filterを実装する | `main()`、`import_run()`、run loop、summary出力関数 | 必須 |
| `tools/final_stage/hairgpu_final_db.py` | 拡張summary/CSV/JSONLをDBへ取り込む。case_id、variant flag、p99、certificate/fallback系列を保存する | `RuntimeRunMetadata`、`FinalStageDatabase.import_runtime_run()` | 必須 |
| `tools/final_stage/schema.sql` | `runtime_runs`、`runtime_frame_metrics` に列を追加する | `CREATE TABLE runtime_runs`、`CREATE TABLE runtime_frame_metrics` | 必須 |
| `tools/final_stage/hairgpu_final_excel.py` | Excel出力に新列と650 summary sheetを追加する | workbook/sheet生成処理 | 推奨 |
| `tools/final_stage/Run_Final_Runtime_Suite.bat` | 既存suite実行bat。650実験用に呼び出し例を追加するか、新batへ分ける | bat全体 | 任意 |
| `tools/final_stage/Run_Research_Runtime_Suite.bat` | 研究用suite実行bat。650実験用のbatとして使う候補 | bat全体 | 任意 |

#### Python側で実装してはいけない場所

| ファイル | 理由 |
|---|---|
| `tools/phase1/*.py` | Phase1用であり、今回のfinal runtime 650実験とは責務が違う |
| `tools/research/hairgpu_research_pipeline.py` | 研究pipeline用だが、既存final runtime suiteがあるため、まずは `tools/final_stage` に集約する |
| `tools/dev/*.py` | 開発補助用であり、実験本体の管理場所ではない |

---

## 0.2 実装内容とファイルの対応表

| 実装内容 | 追加/修正するファイル | 実装する関数・要素 | 受け入れ条件 |
|---|---|---|---|
| 650 case生成 | `tools/final_stage/generate_experiment_650.py` | `generate_feature_combinations()`、`generate_cases()`、`write_jsonl()`、`write_suite_json()`、`main()` | jsonlが650行、suite runsが650件 |
| variant名追加 | `tools/final_stage/generate_experiment_650.py`、`hairgpu_final_run.py`、`hairgpu_runtime_suite.py` | `Combo_00000`〜`Combo_11111`、`Baseline_CurrentProject` | 全variant名がsummary/DB/CSVに残る |
| viewerへcase設定を渡す | `tools/final_stage/hairgpu_final_run.py` | `run_viewer(..., case_config: dict)` | `HAIRGPU_EXPERIMENT_CASE_ID` 等がviewerプロセスへ渡る |
| viewerでcase設定を読む | `GpuHairViewer.h`、`GpuHairViewer.cpp` | `ExperimentCaseConfig`、`LoadExperimentCaseFromEnvironment()` | summary.jsonにcase_id/variant/scenario/seedが出る |
| warmup/measurement分離 | `GpuHairViewer.h`、`GpuHairViewer.cpp` | `m_globalFrameIndex`、`m_measuredFrameIndex`、`ShouldRecordMeasurementFrame()` | warmup frameがCSV/JSONL/summaryに入らない |
| auto-exit | `GpuHairViewer.cpp` | `GpuHairViewer::Run()` 内のframe loop終了条件 | measurement完了後にviewerが自動終了する |
| frames.csv拡張 | `GpuHairViewer.h`、`GpuHairViewer.cpp` | `PerformanceSample` 新列、`ExportPerformanceCsv()` | 既存列を削除せず末尾に新列追加 |
| frames.jsonl追加 | `GpuHairViewer.h`、`GpuHairViewer.cpp` | `ExportPerformanceJsonl()` | `<run_name>_frames.jsonl` が出力される |
| summary.json拡張 | `GpuHairViewer.cpp` | `ExportRuntimeReport()` | p50/p99、case info、feature flags、placeholder metricsが出る |
| DB schema拡張 | `tools/final_stage/schema.sql` | `runtime_runs`、`runtime_frame_metrics` 追加列 | 新DB作成時に拡張列が存在する |
| DB import拡張 | `tools/final_stage/hairgpu_final_db.py` | `RuntimeRunMetadata`、`import_runtime_run()` | 拡張summary/CSV/JSONLを取り込める |
| suite resume | `tools/final_stage/hairgpu_runtime_suite.py` | `--resume`、完了判定関数 | summary.jsonがあるcaseはskipできる |
| fail-policy continue | `tools/final_stage/hairgpu_runtime_suite.py` | `--fail-policy`、例外処理 | 失敗caseでerror.jsonを書き、次caseへ進む |
| case filter | `tools/final_stage/hairgpu_runtime_suite.py` | `--case-filter`、`--variant-filter`、`--scenario-filter` | 部分実行できる |
| 論文用CSV | `tools/final_stage/hairgpu_runtime_suite.py` または `hairgpu_final_excel.py` | `performance_table.csv`、`quality_table.csv`、`ablation_table.csv` 出力 | results配下に表が出る |

---

## 0.3 作業順と実装先の対応

以下の順番で実装する。各Taskでは、**触るファイルを限定する**。

| 順番 | Task | 触るファイル |
|---:|---|---|
| 1 | 650 manifest generator追加 | `tools/final_stage/generate_experiment_650.py` のみ |
| 2 | viewer case config定義 | `GpuHairViewer.h`、`GpuHairViewer.cpp` |
| 3 | warmup/measurement分離 | `GpuHairViewer.h`、`GpuHairViewer.cpp` |
| 4 | frames.csv/jsonl/summary拡張 | `GpuHairViewer.h`、`GpuHairViewer.cpp` |
| 5 | `hairgpu_final_run.py` からcase環境変数を渡す | `tools/final_stage/hairgpu_final_run.py` |
| 6 | suite runnerで650 caseを回す | `tools/final_stage/hairgpu_runtime_suite.py` |
| 7 | DB schema/import拡張 | `tools/final_stage/schema.sql`、`hairgpu_final_db.py` |
| 8 | Excel/論文用CSV拡張 | `hairgpu_runtime_suite.py`、必要なら `hairgpu_final_excel.py` |
| 9 | bat追加または更新 | `tools/final_stage/Run_Research_Runtime_Suite.bat` など |

この順番を守る理由は、C++ viewerがcase設定・measurement出力に対応していない状態でPython runnerだけ拡張しても、650 caseを公平に計測できないためである。

---

## 0.4 各ファイルに入れる内容の詳細

### 0.4.1 `tools/final_stage/generate_experiment_650.py`

このファイルは新規追加する。既存ファイルへ混ぜない。

実装する関数：

```python
def bit_string(mask: int) -> str:
    ...

def make_combo_variant(mask: int) -> dict:
    ...

def default_scenarios() -> list[dict]:
    ...

def generate_cases(seeds: list[int], scenarios: list[dict]) -> list[dict]:
    ...

def write_jsonl(path: Path, cases: list[dict]) -> None:
    ...

def write_suite_json(path: Path, cases: list[dict]) -> None:
    ...

def main() -> int:
    ...
```

出力するcaseの必須key：

```json
{
  "case_id": "case_0001",
  "run_name": "case_0001_Combo_00000_S01_StaticGroom_seed0",
  "method": "Combo_00000",
  "variant": "Combo_00000",
  "scenario": "S01_StaticGroom",
  "seed": 0,
  "warmup_frames": 120,
  "measurement_frames": 600,
  "enableMicrotype": false,
  "enableCBMA": false,
  "enablePhysicsIslands": false,
  "enableRodRepair": false,
  "enableGpuResident": false,
  "output_subdir": "cases/case_0001"
}
```

---

### 0.4.2 `apps/dx11_viewer/src/GpuHairViewer.h`

追加するstruct：

```cpp
struct ExperimentCaseConfig
{
    bool enabled = false;
    bool autoExitAfterCapture = false;
    std::wstring caseId;
    std::wstring variant;
    std::wstring scenario;
    uint32_t seed = 0u;
    uint32_t warmupFrames = 0u;
    uint32_t measurementFrames = 0u;
    bool enableMicrotype = false;
    bool enableCBMA = false;
    bool enablePhysicsIslands = false;
    bool enableRodRepair = false;
    bool enableGpuResident = false;
};
```

`PerformanceSample` に追加するfield：

```cpp
uint32_t globalFrameIndex = 0u;
uint32_t measuredFrameIndex = 0u;
uint32_t warmupFrames = 0u;
uint32_t measurementFrames = 0u;
uint32_t seed = 0u;
uint32_t certificateFailureCount = 0u;
uint32_t fallbackCount = 0u;
uint32_t modeSkipCount = 0u;
uint32_t modeQsCount = 0u;
uint32_t modeDynamicCount = 0u;
uint32_t modeIslandCount = 0u;
uint32_t modeRepairCount = 0u;
uint32_t modeFloatFallbackCount = 0u;
uint32_t islandCount = 0u;
uint32_t islandMemberCount = 0u;
uint32_t islandCommitCount = 0u;
uint32_t islandRejectCount = 0u;
uint32_t repairTaskCount = 0u;
uint32_t repairCommittedCount = 0u;
uint32_t repairRejectedCount = 0u;
uint32_t repairDeferredCount = 0u;
float repairSuccessRate = 0.0f;
float gpuHairTimeMs = 0.0f;
float vramMb = 0.0f;
float bandwidthMbFrame = 0.0f;
uint32_t cpuReadbackCount = 0u;
```

追加するmember/function宣言：

```cpp
ExperimentCaseConfig m_experimentCase;
uint32_t m_globalFrameIndex = 0u;
uint32_t m_measuredFrameIndex = 0u;

void LoadExperimentCaseFromEnvironment();
bool IsExperimentModeEnabled() const;
bool ShouldRecordMeasurementFrame() const;
bool ShouldAutoExitExperiment() const;
bool ExportPerformanceJsonl(const std::wstring& filePath) const;
```

---

### 0.4.3 `apps/dx11_viewer/src/GpuHairViewer.cpp`

主に変更する箇所：

```txt
1. GpuHairViewer::Run() の初期化直後
   -> LoadExperimentCaseFromEnvironment() を呼ぶ。

2. PerformanceSample sample を作って m_performanceSamples.push_back(sample) している箇所
   -> warmup中はpushしない。
   -> measurement中だけpushする。
   -> sampleにcase由来のframe index/seed/placeholder metricsを入れる。

3. frame loop末尾
   -> ShouldAutoExitExperiment() がtrueなら m_running=false にする。

4. HAIRGPU_AUTO_TELEMETRY のexport箇所
   -> CSV、JSONL、summary、settingsを出す。

5. ExportPerformanceCsv()
   -> 既存列は維持し、末尾に新列を追加。

6. ExportRuntimeReport()
   -> p50/p99、case_id、variant、scenario、seed、feature_flags、placeholder metricsを追加。

7. 新規 ExportPerformanceJsonl()
   -> 1 sample 1 JSON objectで出力。
```

注意：`ExportRuntimeReport()` 内では既に `ExportPerformanceCsv()` を呼んでいるため、`Run()` 側でもCSV exportしている場合は二重出力になる。既存仕様を壊さないために許容してよいが、実装者は最終的に以下のどちらかへ統一する。

```txt
案A: Run() は ExportRuntimeReport(summaryPath) だけ呼ぶ。ExportRuntimeReport内でCSV/JSONL/settingsも出す。
案B: Run() がCSV/JSONL/summary/settingsを個別に呼ぶ。ExportRuntimeReport内のCSV再出力は残すが同じ内容になる。
```

推奨は案Aである。

---

### 0.4.4 `tools/final_stage/hairgpu_final_run.py`

変更する関数：

```python
def run_viewer(viewer_exe: Path, telemetry_dir: Path, run_name: str, timeout_seconds: int, case_config: dict[str, Any] | None = None) -> tuple[Path, Path, Path | None]:
    ...
```

追加で設定する環境変数：

```python
env["HAIRGPU_EXPERIMENT_CASE_ID"] = case_config["case_id"]
env["HAIRGPU_VARIANT"] = case_config["variant"]
env["HAIRGPU_SCENARIO"] = case_config["scenario"]
env["HAIRGPU_SEED"] = str(case_config["seed"])
env["HAIRGPU_WARMUP_FRAMES"] = str(case_config["warmup_frames"])
env["HAIRGPU_MEASUREMENT_FRAMES"] = str(case_config["measurement_frames"])
env["HAIRGPU_AUTO_EXIT_AFTER_CAPTURE"] = "1"
env["HAIRGPU_ENABLE_MICROTYPE"] = "1" if case_config["enableMicrotype"] else "0"
env["HAIRGPU_ENABLE_CBMA"] = "1" if case_config["enableCBMA"] else "0"
env["HAIRGPU_ENABLE_PHYSICS_ISLANDS"] = "1" if case_config["enablePhysicsIslands"] else "0"
env["HAIRGPU_ENABLE_ROD_REPAIR"] = "1" if case_config["enableRodRepair"] else "0"
env["HAIRGPU_ENABLE_GPU_RESIDENT"] = "1" if case_config["enableGpuResident"] else "0"
```

`wait_for_file()` の対象：

```txt
<run_name>_summary.json
<run_name>_frames.csv
<run_name>_frames.jsonl
```

ただし移行期間は、`frames.jsonl` がなくても警告に留めてよい。

---

### 0.4.5 `tools/final_stage/hairgpu_runtime_suite.py`

変更する機能：

```txt
1. runtime_suite_650.json の runs を読む。
2. 各runの output_subdir に telemetry_dir を分ける。
3. run_viewer(..., case_config=run) を呼ぶ。
4. summary.json / frames.csv / frames.jsonl / settings.json をcase folderへ保存する。
5. --resume 指定時は成功済みcaseをskipする。
6. --fail-policy continue 指定時はerror.jsonを書いて次へ進む。
7. --case-filter / --variant-filter / --scenario-filter を追加する。
8. 全case完了後に summary_all.csv 等を出す。
```

追加CLI：

```bash
--suite results/exp_650/runtime_suite_650.json
--viewer-exe bin/Debug/HairSystem/HairSystem.exe
--output results/exp_650
--resume
--fail-policy continue
--case-filter case_0001,case_0002
--variant-filter Combo_11111
--scenario-filter S05_ContactHeavy_Sphere
```

---

### 0.4.6 `tools/final_stage/schema.sql`

`runtime_runs` に追加する列：

```sql
case_id TEXT,
variant_code TEXT,
seed INTEGER,
warmup_frames INTEGER,
measurement_frames INTEGER,
enable_microtype INTEGER,
enable_cbma INTEGER,
enable_physics_islands INTEGER,
enable_rod_repair INTEGER,
enable_gpu_resident INTEGER,
frame_jsonl_path TEXT,
p50_frame_ms REAL,
p99_frame_ms REAL,
gpu_hair_ms REAL,
vram_mb REAL,
bandwidth_mb_frame REAL,
certificate_failure_rate REAL,
fallback_rate REAL,
repair_success_rate REAL,
cpu_readback_count INTEGER
```

`runtime_frame_metrics` に追加する列：

```sql
global_frame_index INTEGER,
measured_frame_index INTEGER,
certificate_failure_count INTEGER,
fallback_count INTEGER,
mode_skip_count INTEGER,
mode_qs_count INTEGER,
mode_dynamic_count INTEGER,
mode_island_count INTEGER,
mode_repair_count INTEGER,
mode_float_fallback_count INTEGER,
island_count INTEGER,
island_member_count INTEGER,
repair_task_count INTEGER,
repair_success_rate REAL,
gpu_hair_time_ms REAL,
vram_mb REAL,
bandwidth_mb_frame REAL,
cpu_readback_count INTEGER
```

---

### 0.4.7 `tools/final_stage/hairgpu_final_db.py`

変更する場所：

```txt
1. RuntimeRunMetadata dataclass
2. FinalStageDatabase.import_runtime_run()
3. CSV row parsing部分
4. INSERT INTO runtime_runs
5. INSERT INTO runtime_frame_metrics
```

`RuntimeRunMetadata` に追加するfield：

```python
case_id: str = ""
variant_code: str = ""
seed: int = 0
warmup_frames: int = 0
measurement_frames: int = 0
enable_microtype: bool = False
enable_cbma: bool = False
enable_physics_islands: bool = False
enable_rod_repair: bool = False
enable_gpu_resident: bool = False
frame_jsonl_path: str = ""
```

移行上の注意：既存CSVに新列がない場合も import が落ちないよう、`_pick_csv_value(..., default="0")` を使う。

---

## 0.5 最小実装と完全実装の境界

いきなり全ファイルを完全対応させず、以下の2段階に分ける。

### 最小実装

```txt
1. generate_experiment_650.py が650 caseを生成する。
2. GpuHairViewerがcase_id/variant/scenario/seedをsummary.jsonへ出す。
3. warmup frameを除外する。
4. measurement完了後にauto-exitする。
5. hairgpu_runtime_suite.py が650 caseを順番に起動できる。
6. 失敗caseで止まらない。
```

### 完全実装

```txt
1. frames.jsonlも出す。
2. summary.jsonにp50/p95/p99、feature flags、placeholder metricsを出す。
3. SQLiteへ全拡張列をimportする。
4. Excel/CSVで論文用表を出す。
5. filter/resume/rerun-failedが動く。
```

この境界を明確にすることで、まず最小実装で650 caseの実行制御を確認し、その後にDBやExcel出力を拡張できる。


## 0. 結論

前回の `Non_Novelty_Infrastructure_Implementation_Design.md` は、実装方針としては使えるが、**作業単位と変更箇所が不足していた**。

不足していた点は以下である。この曖昧さが実装の発散につながる。

```txt
1. 既存ファイルのどこを変更するかが曖昧だった。
2. C++側の既存関数名・既存struct名・出力関数名に接続していなかった。
3. Python runner側の既存処理に対して、追加引数・環境変数・resume/error処理の差分が明確でなかった。
4. SQLite schemaとimport処理の対応が不足していた。
5. 「650パターン生成」は説明だけで、実装可能な完全コードの形になっていなかった。
```

この修正版では、`S.zip` の現在の構成を前提に、**段階的に実装・レビューできる粒度**に落とす。対象は新規性5要素そのものではなく、650パターン自動実験・telemetry・集計・baseline/variant管理のための周辺基盤である。

---

## 1. 対象外にするもの

以下は、この設計書では実装しない。

```txt
1. Physics-Certified Strand Microtype の中身
2. Counterfactual Budgeted Mode Allocation の中身
3. Self-Materializing Strand Physics Islands の中身
4. Certificate-Driven Rod Repair の中身
5. GPU-Resident Strand Runtime の中身
```

ただし、これらを将来ON/OFFできるように、flag、variant名、ログ列、DB列は先に用意する。未実装時は `0` または `null` を出力する。

---

## 2. 現在の実装で直接接続する既存箇所

### 2.1 C++ viewer

対象ファイル：

```txt
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer.cpp
```

既に存在する重要箇所：

```txt
GpuHairViewer::Run()
PerformanceSample
m_performanceSamples
ExportPerformanceCsv()
ExportRuntimeReport()
HAIRGPU_AUTO_TELEMETRY
HAIRGPU_TELEMETRY_DIR
HAIRGPU_TELEMETRY_RUN_NAME
```

修正方針：

```txt
1. 既存のm_performanceSamplesを使い続ける。
2. PerformanceSampleを拡張する。
3. warmup frameはm_performanceSamplesへ入れない。
4. measurement frameだけCSV/JSONL/summaryに入れる。
5. summary.jsonにcase_id / variant / scenario / seed / feature flags / p99を追加する。
6. frames.jsonlを追加出力する。
7. 自動実験時はmeasurement完了後にm_running=falseへしてviewerを自動終了する。
```

### 2.2 Python runner

対象ファイル：

```txt
tools/final_stage/hairgpu_final_run.py
tools/final_stage/hairgpu_runtime_suite.py
tools/final_stage/hairgpu_final_db.py
tools/final_stage/schema.sql
```

既に存在する重要箇所：

```txt
hairgpu_final_run.run_viewer()
hairgpu_runtime_suite.import_run()
hairgpu_runtime_suite.main()
FinalStageDatabase.import_runtime_run()
runtime_runs table
runtime_frame_metrics table
```

修正方針：

```txt
1. run_viewer()がcase情報を環境変数としてviewerへ渡せるようにする。
2. runtime_suiteが650 caseを順番に実行できるようにする。
3. resume=trueならsummary/frameが既にあるcaseをskipする。
4. fail_policy=continueなら失敗caseをerror.jsonに残して次へ進む。
5. DBへp50/p95/p99、feature flags、case_id、seed、certificate/fallback列を保存する。
```

---

## 3. 実装の前提と作業方針

この設計書は、既存コードへ段階的に安全な差分を入れるための作業仕様である。実装方法や担当者に依存せず、既存挙動の維持、ログ互換性、650パターン実験の再現性を優先する。

実装では、以下の方針を守る。

```txt
1. 既存のviewer挙動を壊さない。
2. 既存CSV列は削除・改名しない。新規列は末尾追加にする。
3. HAIRGPU_EXPERIMENT_CASE_ID が未指定なら、通常のviewerとして動く。
4. 新規性5要素の内部処理は実装しない。ログ列とflagだけ用意する。
5. 未実装の新規性系metricは 0 または null として出す。
6. 1回の修正で全体を書き換えず、下記の作業単位ごとに実装・確認する。
7. Python側は既存CLIとの互換性を維持する。
8. C++側は既存のtelemetry export経路を流用する。
```

---

## 4. 作業単位

以下の順番で小さく進める。各作業単位は、それ単独でビルドまたは実行確認できることを目標にする。

### Task 0: 既存構成の確認

対象：リポジトリ全体

確認するもの：

```txt
1. apps/dx11_viewer/src/GpuHairViewer.h が存在する。
2. apps/dx11_viewer/src/GpuHairViewer.cpp が存在する。
3. tools/final_stage/hairgpu_final_run.py が存在する。
4. tools/final_stage/hairgpu_runtime_suite.py が存在する。
5. tools/final_stage/hairgpu_final_db.py が存在する。
6. tools/final_stage/schema.sql が存在する。
7. HAIRGPU_AUTO_TELEMETRY による既存telemetry出力が動く。
```

完了条件：

```txt
既存のsummary.jsonとframes.csvが、修正前と同じ手順で出力できる。
```

### Task 1: manifest generatorを追加する

対象：

```txt
tools/final_stage/generate_experiment_650.py
```

実装内容：

```txt
1. 32 feature combinationsを生成する。
2. 10 scenariosを生成する。
3. 2 seedsを生成する。
4. Baseline_CurrentProjectを10件追加する。
5. 合計650件をjsonlとsuite jsonで出力する。
```

完了条件：

```bash
python tools/final_stage/generate_experiment_650.py   --out-jsonl results/exp_650/experiments_650.jsonl   --out-suite results/exp_650/runtime_suite_650.json
```

```txt
1. experiments_650.jsonl の行数が650である。
2. runtime_suite_650.json の runs 数が650である。
3. Combo_00000 と Combo_11111 が含まれる。
4. Baseline_CurrentProject が10件だけ含まれる。
```

### Task 2: viewerがcase設定を環境変数から読めるようにする

対象：

```txt
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer.cpp
```

実装内容：

```txt
1. ExperimentCaseConfigを追加する。
2. LoadExperimentCaseFromEnvironment()を追加する。
3. case_id, variant, scenario, seed, warmup_frames, measurement_framesを読む。
4. feature flagsを読む。
5. 未指定時はenabled=falseとして通常viewer挙動にする。
```

完了条件：

```txt
1. 環境変数未指定時に既存viewerが通常起動する。
2. 環境変数指定時にsummary.jsonへcase_id / variant / scenario / seedが出る。
```

### Task 3: warmup / measurement分離を実装する

対象：

```txt
GpuHairViewer::Run()
PerformanceSample
m_performanceSamples
```

実装内容：

```txt
1. globalFrameIndexを持つ。
2. warmup frameはm_performanceSamplesに追加しない。
3. measurement frameのみm_performanceSamplesに追加する。
4. measuredFrameIndexを0から振る。
5. measurement完了後、HAIRGPU_AUTO_EXIT_AFTER_CAPTURE=1ならviewerを終了する。
```

完了条件：

```txt
warmup=120, measurement=600 の場合、frames.csv / frames.jsonl に600 frame分だけ出る。
```

### Task 4: telemetry schemaを拡張する

対象：

```txt
PerformanceSample
ExportPerformanceCsv()
ExportRuntimeReport()
```

実装内容：

```txt
1. 既存列を維持する。
2. 新規列を末尾追加する。
3. p50/p95/p99をsummaryに出す。
4. 新規性未実装項目は0またはnullで出す。
```

完了条件：

```txt
1. 既存のframes.csv readerが壊れない。
2. 新規列が末尾に追加されている。
3. summary.jsonにp99_frame_msが出る。
```

### Task 5: frames.jsonlを追加する

対象：

```txt
GpuHairViewer.cpp
```

実装内容：

```txt
1. ExportPerformanceJsonl()を追加する。
2. 1 frame 1 JSON objectで出力する。
3. case_id / variant / scenario / seedを各行に含める。
4. mode_countsのようなネスト構造はJSON objectとして出す。
```

完了条件：

```txt
frames.jsonlが出力され、行数がmeasurement frame数と一致する。
```

### Task 6: Python runnerからcase設定をviewerへ渡す

対象：

```txt
tools/final_stage/hairgpu_final_run.py
tools/final_stage/hairgpu_runtime_suite.py
```

実装内容：

```txt
1. run_viewer()の引数にcase情報を追加する。
2. viewer起動時のenvにHAIRGPU_EXPERIMENT_CASE_ID等を追加する。
3. runtime_suite_650.jsonの各runからcase情報を取り出して渡す。
```

完了条件：

```txt
runtime_suite_650.jsonの1 caseだけを実行し、summary.jsonに同じcase_idが出る。
```

### Task 7: resume / fail-policy continueを実装する

対象：

```txt
tools/final_stage/hairgpu_runtime_suite.py
```

実装内容：

```txt
1. --resumeを追加または強化する。
2. summary.jsonとframes.csv/jsonlが存在するcaseはskipする。
3. --fail-policy continue時、case失敗でerror.jsonを書き、次caseへ進む。
4. --case-filter, --variant-filter, --scenario-filterを追加する。
```

完了条件：

```txt
1. 途中失敗しても全体が止まらない。
2. 再実行時に完了済みcaseをskipする。
3. filter指定で一部caseだけ再実行できる。
```

### Task 8: SQLite schema / importを拡張する

対象：

```txt
tools/final_stage/schema.sql
tools/final_stage/hairgpu_final_db.py
```

実装内容：

```txt
1. runtime_runsにcase_id, seed, p99, feature flagsを追加する。
2. runtime_frame_metricsにcertificate/fallback/mode/island/repair系列を追加する。
3. summary.jsonとframes.csv/jsonlから新規列をimportする。
4. 古いsummary/csvでも可能な範囲でimportできるようにする。
```

完了条件：

```txt
1. 新しいcaseのsummaryとframesがSQLiteへ入る。
2. p99_frame_msでSQL集計できる。
3. case_idでrunを識別できる。
```

### Task 9: 論文用CSV出力を拡張する

対象：

```txt
tools/final_stage/hairgpu_runtime_suite.py
```

実装内容：

```txt
1. performance_table.csvを出す。
2. quality_table.csvを出す。
3. ablation_table.csvを出す。
4. scenario_table.csvを出す。
5. failed_cases.csvを出す。
```

完了条件：

```txt
650件すべてのsummaryから、論文に貼れる表の下書きCSVが出る。
```

---

## 5. レビュー観点

レビューでは、以下を確認する。

### 5.1 互換性レビュー

```txt
1. 既存のHAIRGPU_AUTO_TELEMETRYが壊れていない。
2. 既存のframes.csv列が消えていない。
3. 環境変数未指定時のviewer挙動が変わっていない。
4. 既存のruntime_suite configが読める。
```

### 5.2 実験再現性レビュー

```txt
1. case_idがsummary/csv/jsonl/DBで一致する。
2. seedがsummary/csv/jsonl/DBで一致する。
3. warmup frameが集計に混ざっていない。
4. measurement frame数がcase設定と一致する。
5. settings_hashがsummaryに残る。
```

### 5.3 650パターン実行レビュー

```txt
1. manifestが650件である。
2. Baseline_CurrentProjectが10件である。
3. Combo_00000〜Combo_11111が各scenario/seedで揃っている。
4. --resumeで完了済みcaseをskipする。
5. --fail-policy continueで失敗caseを記録して続行する。
```

### 5.4 ログschemaレビュー

```txt
1. 新規性未実装値は0またはnullで一貫している。
2. p50/p95/p99が同じframe集合から計算されている。
3. certificate/fallback/mode/island/repair列が全variantで存在する。
4. CSVとJSONLで同じframe数が出る。
```

---

## 6. 実装時の禁止事項

```txt
1. 650パターン実験のためにviewer全体を書き換えない。
2. 既存telemetry出力を削除しない。
3. 新規性5要素の内部処理を仮実装しない。
4. Combo_00000をBaseline_CurrentProjectと同一扱いにしない。
5. warmup frameをsummary計算に混ぜない。
6. エラーcaseで実験全体を強制終了しない。
7. Python側だけ直してC++側のcase_id出力を忘れない。
8. C++側だけ直してDB importを忘れない。
```

---

## 7. 作業管理用チェックリスト

以下をチェックしながら進める。

```txt
[ ] generate_experiment_650.pyで650件生成できる
[ ] runtime_suite_650.jsonが既存runnerで読める
[ ] viewerがcase_idを環境変数から読める
[ ] 環境変数未指定時に通常viewerが壊れない
[ ] warmup / measurement分離ができている
[ ] auto-exitがmeasurement完了後に動く
[ ] frames.csvの既存列が残っている
[ ] frames.jsonlが出る
[ ] summary.jsonにp50/p95/p99が出る
[ ] feature flagsがsummary/csv/jsonlに出る
[ ] 新規性未実装metricが0/nullで出る
[ ] --resumeが動く
[ ] --fail-policy continueが動く
[ ] error.jsonが出る
[ ] SQLiteへcase_idとp99が入る
[ ] 論文用CSVが出る
```

---

## 8. 追加するファイル

### 3.1 `tools/final_stage/generate_experiment_650.py`

役割：650パターンのmanifestを自動生成する。

出力：

```txt
experiments_650.jsonl
runtime_suite_650.json
```

生成式：

```txt
32 feature combinations × 10 scenarios × 2 seeds = 640
Baseline_CurrentProject × 10 scenarios = 10
Total = 650
```

feature bit対応：

```txt
bit 0: enableMicrotype
bit 1: enableCBMA
bit 2: enablePhysicsIslands
bit 3: enableRodRepair
bit 4: enableGpuResident
```

variant名：

```txt
Combo_00000
Combo_00001
...
Combo_11111
Baseline_CurrentProject
```

このファイルは、別添の `generate_experiment_650.py` を初期実装として使用する。このファイルを起点にテストと出力確認を行う。

---

## 9. `GpuHairViewer.h` の修正仕様

### 4.1 `PerformanceSample` に追加するフィールド

既存の `PerformanceSample` に以下を追加する。

```cpp
uint32_t globalFrameIndex = 0u;
uint32_t measuredFrameIndex = 0u;
uint32_t warmupFrames = 0u;
uint32_t measurementFrames = 0u;

uint32_t certificateFailureCount = 0u;
float certificateFailureRate = 0.0f;
uint32_t fallbackCount = 0u;

uint32_t modeSkipCount = 0u;
uint32_t modeQSOnlyCount = 0u;
uint32_t modeDynamicCount = 0u;
uint32_t modeIslandCount = 0u;
uint32_t modeRepairCount = 0u;
uint32_t modeFloatFallbackCount = 0u;

uint32_t islandCount = 0u;
uint32_t islandMemberCount = 0u;
uint32_t islandCommitCount = 0u;
uint32_t islandRejectCount = 0u;

uint32_t repairTaskCount = 0u;
uint32_t repairCommittedCount = 0u;
uint32_t repairRejectedCount = 0u;
uint32_t repairDeferredCount = 0u;
float repairSuccessRate = 0.0f;

uint32_t cpuReadbackCount = 0u;
float gpuHairMilliseconds = 0.0f;
float vramMegabytes = 0.0f;
float bandwidthMegabytesPerFrame = 0.0f;

float meanDecodeErrorMm = 0.0f;
float maxPenetrationMm = 0.0f;
```

新規性が未実装の場合、上記の多くは `0` でよい。

### 4.2 新規structを追加

`GpuHairViewer` class内のprivate領域に以下を追加する。

```cpp
struct ExperimentCaseConfig
{
    bool enabled = false;
    std::wstring caseId;
    std::wstring variant;
    std::wstring scenario;
    uint32_t seed = 0u;
    uint32_t warmupFrames = 0u;
    uint32_t measurementFrames = 0u;
    bool autoExitAfterCapture = false;

    bool enableMicrotype = false;
    bool enableCBMA = false;
    bool enablePhysicsIslands = false;
    bool enableRodRepair = false;
    bool enableGpuResident = false;

    std::wstring settingsHash;
};
```

### 4.3 private memberを追加

```cpp
ExperimentCaseConfig m_experimentCase;
uint32_t m_globalFrameIndex = 0u;
uint32_t m_measuredFrameIndex = 0u;
```

### 4.4 private function declarationを追加

```cpp
void LoadExperimentCaseFromEnvironment();
bool ShouldCaptureCurrentFrame() const;
bool ShouldAutoExitAfterCurrentFrame() const;
PerformanceSample BuildPerformanceSample(float dt, float instantFps) const;
bool ExportPerformanceJsonl(const std::wstring& filePath) const;
bool ExportExperimentSettingsJson(const std::wstring& filePath) const;
```

---

## 10. `GpuHairViewer.cpp` の修正仕様

### 5.1 `Run()` の冒頭でcase設定を読む

`GpuHairViewer::Run()` の先頭、`MSG msg = {};` の直後に追加する。

```cpp
LoadExperimentCaseFromEnvironment();
```

### 5.2 frame sample追加部分を置き換える

現在は毎frame必ず以下のようにpushしている。

```cpp
PerformanceSample sample = {};
...
m_performanceSamples.push_back(sample);
```

これを以下の構造へ置き換える。

```cpp
PerformanceSample sample = BuildPerformanceSample(dt, instantFps);

if (ShouldCaptureCurrentFrame())
{
    sample.measuredFrameIndex = m_measuredFrameIndex++;
    m_performanceSamples.push_back(sample);
    if (m_performanceSamples.size() > 12000)
    {
        m_performanceSamples.erase(
            m_performanceSamples.begin(),
            m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
    }
}

++m_globalFrameIndex;

if (ShouldAutoExitAfterCurrentFrame())
{
    m_running = false;
}
```

### 5.3 `ShouldCaptureCurrentFrame()` 実装

```cpp
bool GpuHairViewer::ShouldCaptureCurrentFrame() const
{
    if (!m_experimentCase.enabled)
    {
        return true;
    }
    if (m_globalFrameIndex < m_experimentCase.warmupFrames)
    {
        return false;
    }
    const uint32_t measured = m_globalFrameIndex - m_experimentCase.warmupFrames;
    return measured < m_experimentCase.measurementFrames;
}
```

### 5.4 `ShouldAutoExitAfterCurrentFrame()` 実装

```cpp
bool GpuHairViewer::ShouldAutoExitAfterCurrentFrame() const
{
    if (!m_experimentCase.enabled || !m_experimentCase.autoExitAfterCapture)
    {
        return false;
    }
    const uint32_t totalFrames = m_experimentCase.warmupFrames + m_experimentCase.measurementFrames;
    return m_globalFrameIndex + 1u >= totalFrames;
}
```

### 5.5 `LoadExperimentCaseFromEnvironment()` 実装方針

既存の `GetEnvironmentVariableString()` を使う。boolは `1`, `true`, `TRUE`, `yes`, `YES` をtrue扱いにする。

読む環境変数：

```txt
HAIRGPU_EXPERIMENT_CASE_ID
HAIRGPU_VARIANT
HAIRGPU_SCENARIO
HAIRGPU_SEED
HAIRGPU_WARMUP_FRAMES
HAIRGPU_MEASUREMENT_FRAMES
HAIRGPU_AUTO_EXIT_AFTER_CAPTURE
HAIRGPU_ENABLE_MICROTYPE
HAIRGPU_ENABLE_CBMA
HAIRGPU_ENABLE_PHYSICS_ISLANDS
HAIRGPU_ENABLE_ROD_REPAIR
HAIRGPU_ENABLE_GPU_RESIDENT
HAIRGPU_SETTINGS_HASH
```

### 5.6 `BuildPerformanceSample()` 実装方針

現在 `Run()` 内にある `PerformanceSample sample = {};` から各fieldを詰める処理を、この関数へ移動する。

既存項目はそのまま移す。

追加項目は以下のように入れる。

```cpp
sample.globalFrameIndex = m_globalFrameIndex;
sample.warmupFrames = m_experimentCase.warmupFrames;
sample.measurementFrames = m_experimentCase.measurementFrames;

sample.certificateFailureCount = 0u;
sample.certificateFailureRate = 0.0f;
sample.fallbackCount = 0u;

sample.modeSkipCount = 0u;
sample.modeQSOnlyCount = sample.activeSetSelectedStrandCount;
sample.modeDynamicCount = 0u;
sample.modeIslandCount = 0u;
sample.modeRepairCount = sample.correctiveStrandCount;
sample.modeFloatFallbackCount = 0u;

sample.islandCount = 0u;
sample.islandMemberCount = 0u;
sample.islandCommitCount = 0u;
sample.islandRejectCount = 0u;

sample.repairTaskCount = sample.correctiveStrandCount;
sample.repairCommittedCount = sample.correctiveStrandCount;
sample.repairRejectedCount = 0u;
sample.repairDeferredCount = 0u;
sample.repairSuccessRate = sample.repairTaskCount > 0u
    ? static_cast<float>(sample.repairCommittedCount) / static_cast<float>(sample.repairTaskCount)
    : 0.0f;

sample.cpuReadbackCount = 0u;
sample.gpuHairMilliseconds = sample.simulationMilliseconds;
sample.vramMegabytes = m_activeSetPlan.summary.estimatedMemoryMegabytes;
sample.bandwidthMegabytesPerFrame = 0.0f;
sample.meanDecodeErrorMm = 0.0f;
sample.maxPenetrationMm = sample.correctiveMaxPenetration * 1000.0f;
```

注意：上記は新規性未実装時の暫定値である。新規性実装後は各runtime passの実測値へ置き換える。

---

## 11. `ExportPerformanceCsv()` の拡張

既存ヘッダ末尾に以下を追加する。

```txt
globalFrameIndex,measuredFrameIndex,warmupFrames,measurementFrames,
caseId,variant,scenario,seed,settingsHash,
enableMicrotype,enableCBMA,enablePhysicsIslands,enableRodRepair,enableGpuResident,
certificateFailureCount,certificateFailureRate,fallbackCount,
modeSkipCount,modeQSOnlyCount,modeDynamicCount,modeIslandCount,modeRepairCount,modeFloatFallbackCount,
islandCount,islandMemberCount,islandCommitCount,islandRejectCount,
repairTaskCount,repairCommittedCount,repairRejectedCount,repairDeferredCount,repairSuccessRate,
cpuReadbackCount,gpuHairMilliseconds,vramMegabytes,bandwidthMegabytesPerFrame,
meanDecodeErrorMm,maxPenetrationMm
```

CSVは既存Python importとの互換性を保つため、既存列を削除しない。新規列は末尾追加とする。

---

## 12. `ExportPerformanceJsonl()` の追加

`summary.json` と `frames.csv` に加えて、以下を出す。

```txt
<run_name>_frames.jsonl
```

1行1frame：

```json
{
  "case_id": "case_0001",
  "variant": "Combo_00000",
  "scenario": "S01_StaticGroom",
  "seed": 0,
  "settings_hash": "...",
  "global_frame_index": 120,
  "measured_frame_index": 0,
  "frame_time_ms": 16.1,
  "simulation_time_ms": 5.2,
  "gpu_hair_time_ms": 5.2,
  "vram_mb": 128.0,
  "certificate_failure_count": 0,
  "fallback_count": 0,
  "mode_counts": {
    "skip": 0,
    "qs_only": 100000,
    "dynamic": 0,
    "island": 0,
    "repair": 0,
    "float_fallback": 0
  },
  "island_count": 0,
  "repair_task_count": 0,
  "repair_success_rate": 0.0,
  "max_penetration_mm": 0.0,
  "mean_decode_error_mm": 0.0
}
```

---

## 13. `ExportRuntimeReport()` の拡張

### 8.1 p50/p99計算を追加

既存は平均、p95、worstを出している。以下を追加する。

```txt
p50_frame_ms
p99_frame_ms
p50_simulation_ms
p99_simulation_ms
```

実装関数：

```cpp
float ComputePercentile(std::vector<float> values, float percentile)
{
    if (values.empty()) return 0.0f;
    std::sort(values.begin(), values.end());
    const float clamped = std::clamp(percentile, 0.0f, 100.0f);
    const float rank = (clamped / 100.0f) * static_cast<float>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(rank));
    const size_t hi = static_cast<size_t>(std::ceil(rank));
    if (lo == hi) return values[lo];
    const float t = rank - static_cast<float>(lo);
    return values[lo] * (1.0f - t) + values[hi] * t;
}
```

既存の `ComputePercentile95()` は残してもよいが、内部で `ComputePercentile(values, 95.0f)` を使う形にしてよい。

### 8.2 summary.jsonへ追加する項目

```json
{
  "case_id": "case_0001",
  "variant": "Combo_00000",
  "scenario": "S01_StaticGroom",
  "seed": 0,
  "warmup_frames": 120,
  "measurement_frames": 600,
  "settings_hash": "...",
  "feature_flags": {
    "enableMicrotype": false,
    "enableCBMA": false,
    "enablePhysicsIslands": false,
    "enableRodRepair": false,
    "enableGpuResident": false
  },
  "p50_frame_ms": 0.0,
  "p95_frame_ms": 0.0,
  "p99_frame_ms": 0.0,
  "avg_gpu_hair_ms": 0.0,
  "p95_gpu_hair_ms": 0.0,
  "p99_gpu_hair_ms": 0.0,
  "vram_mb": 0.0,
  "bandwidth_mb_frame": 0.0,
  "certificate_failure_rate": 0.0,
  "fallback_rate": 0.0,
  "repair_success_rate": 0.0,
  "cpu_readback_count": 0,
  "frame_jsonl": "..."
}
```

---

## 14. telemetry自動出力部分の修正

`GpuHairViewer::Run()` 終了直前の自動telemetry出力を以下に拡張する。

既存：

```cpp
const std::filesystem::path summaryPath = telemetryDir / (runName + L"_summary.json");
const std::filesystem::path frameCsvPath = telemetryDir / (runName + L"_frames.csv");
ExportPerformanceCsv(frameCsvPath.wstring());
ExportRuntimeReport(summaryPath.wstring());
```

修正後：

```cpp
const std::filesystem::path summaryPath = telemetryDir / (runName + L"_summary.json");
const std::filesystem::path frameCsvPath = telemetryDir / (runName + L"_frames.csv");
const std::filesystem::path frameJsonlPath = telemetryDir / (runName + L"_frames.jsonl");
const std::filesystem::path settingsJsonPath = telemetryDir / (runName + L"_settings.json");

ExportPerformanceCsv(frameCsvPath.wstring());
ExportPerformanceJsonl(frameJsonlPath.wstring());
ExportExperimentSettingsJson(settingsJsonPath.wstring());
ExportRuntimeReport(summaryPath.wstring());
```

`ExportRuntimeReport()` 内でも `frames.jsonl` と `settings.json` のpathをexportsへ入れる。

---

## 15. `hairgpu_final_run.py` の修正仕様

### 10.1 `METHOD_ALIASES` を拡張

既存aliasに以下を追加する。

```python
METHOD_ALIASES.update({
    "Baseline_CurrentProject": "Baseline_CurrentProject",
    "baseline_current_project": "Baseline_CurrentProject",
    "Proposed_FullSystem": "Proposed_FullSystem",
    "proposed_full_system": "Proposed_FullSystem",
})
for mask in range(32):
    name = f"Combo_{mask:05b}"
    METHOD_ALIASES[name] = name
    METHOD_ALIASES[name.lower()] = name
```

### 10.2 `run_viewer()` のsignature変更

既存：

```python
def run_viewer(viewer_exe: Path, telemetry_dir: Path, run_name: str, timeout_seconds: int) -> tuple[Path, Path]:
```

修正後：

```python
def run_viewer(
    viewer_exe: Path,
    telemetry_dir: Path,
    run_name: str,
    timeout_seconds: int,
    case: dict[str, Any] | None = None,
) -> tuple[Path, Path, Path | None, Path | None]:
```

返り値：

```txt
summary_json
frame_csv
frame_jsonl
settings_json
```

### 10.3 viewerへ渡す環境変数

`case` がある場合、以下をenvへ入れる。

```python
env["HAIRGPU_EXPERIMENT_CASE_ID"] = str(case.get("case_id", ""))
env["HAIRGPU_VARIANT"] = str(case.get("method", case.get("variant", "")))
env["HAIRGPU_SCENARIO"] = str(case.get("scenario", ""))
env["HAIRGPU_SEED"] = str(case.get("seed", 0))
env["HAIRGPU_WARMUP_FRAMES"] = str(case.get("warmup_frames", 0))
env["HAIRGPU_MEASUREMENT_FRAMES"] = str(case.get("measurement_frames", 0))
env["HAIRGPU_AUTO_EXIT_AFTER_CAPTURE"] = "1"
env["HAIRGPU_ENABLE_MICROTYPE"] = "1" if case.get("enableMicrotype") else "0"
env["HAIRGPU_ENABLE_CBMA"] = "1" if case.get("enableCBMA") else "0"
env["HAIRGPU_ENABLE_PHYSICS_ISLANDS"] = "1" if case.get("enablePhysicsIslands") else "0"
env["HAIRGPU_ENABLE_ROD_REPAIR"] = "1" if case.get("enableRodRepair") else "0"
env["HAIRGPU_ENABLE_GPU_RESIDENT"] = "1" if case.get("enableGpuResident") else "0"
env["HAIRGPU_SETTINGS_HASH"] = str(case.get("settings_hash", ""))
```

---

## 16. `hairgpu_runtime_suite.py` の修正仕様

### 11.1 CLI options追加

```python
parser.add_argument('--resume', action='store_true')
parser.add_argument('--fail-policy', choices=['stop', 'continue'], default='stop')
parser.add_argument('--case-filter')
parser.add_argument('--variant-filter')
parser.add_argument('--scenario-filter')
parser.add_argument('--rerun-failed', action='store_true')
```

### 11.2 run filtering

`config['runs']` を読む直後に、以下条件でfilterする。

```txt
case-filter が指定されたら case_id一致のみ
variant-filter が指定されたら method一致のみ
scenario-filter が指定されたら scenario一致のみ
```

### 11.3 resume判定

caseが完了済みかどうかは以下で判定する。

```txt
summary_json exists and size > 0
frame_csv exists and size > 0
```

`frames.jsonl` は研究評価用なので、存在しなければwarning扱いでよい。ただしC++側実装後は必須にしてよい。

### 11.4 fail-policy continue

`import_run()` をtry/exceptで囲む。

失敗時：

```txt
1. case output_dirを作る
2. error.jsonを書く
3. fail_policy == continue なら次へ進む
4. fail_policy == stop ならraiseする
```

error.json schema：

```json
{
  "case_id": "case_0001",
  "run_name": "case_0001_Combo_00000_S01_StaticGroom_seed0",
  "method": "Combo_00000",
  "scenario": "S01_StaticGroom",
  "seed": 0,
  "error_type": "TimeoutError",
  "message": "Timed out waiting for summary json",
  "stage": "viewer_or_import"
}
```

---

## 17. `schema.sql` の拡張仕様

### 12.1 `runtime_runs` に追加する列

既存tableを破壊しないため、schema.sqlのCREATE TABLEへ列を追加する。既存DB移行用には `ALTER TABLE IF NOT EXISTS` 相当のmigrate処理を別途Pythonで入れるのが望ましい。

追加列：

```sql
case_id TEXT,
seed INTEGER DEFAULT 0,
settings_hash TEXT,
warmup_frames INTEGER DEFAULT 0,
measurement_frames INTEGER DEFAULT 0,
enable_microtype INTEGER DEFAULT 0,
enable_cbma INTEGER DEFAULT 0,
enable_physics_islands INTEGER DEFAULT 0,
enable_rod_repair INTEGER DEFAULT 0,
enable_gpu_resident INTEGER DEFAULT 0,
p50_frame_ms REAL,
p99_frame_ms REAL,
avg_gpu_hair_ms REAL,
p95_gpu_hair_ms REAL,
p99_gpu_hair_ms REAL,
vram_mb REAL,
bandwidth_mb_frame REAL,
certificate_failure_rate REAL,
fallback_rate REAL,
repair_success_rate REAL,
cpu_readback_count INTEGER DEFAULT 0,
frame_jsonl_path TEXT,
settings_json_path TEXT,
error_json_path TEXT
```

### 12.2 `runtime_frame_metrics` に追加する列

```sql
global_frame_index INTEGER,
measured_frame_index INTEGER,
certificate_failure_count INTEGER DEFAULT 0,
certificate_failure_rate REAL DEFAULT 0,
fallback_count INTEGER DEFAULT 0,
mode_skip_count INTEGER DEFAULT 0,
mode_qs_count INTEGER DEFAULT 0,
mode_dynamic_count INTEGER DEFAULT 0,
mode_island_count INTEGER DEFAULT 0,
mode_repair_count INTEGER DEFAULT 0,
mode_float_fallback_count INTEGER DEFAULT 0,
island_count INTEGER DEFAULT 0,
island_member_count INTEGER DEFAULT 0,
repair_task_count INTEGER DEFAULT 0,
repair_success_rate REAL DEFAULT 0,
cpu_readback_count INTEGER DEFAULT 0,
gpu_hair_ms REAL DEFAULT 0,
vram_mb REAL DEFAULT 0,
bandwidth_mb_frame REAL DEFAULT 0,
mean_decode_error_mm REAL DEFAULT 0,
max_penetration_mm REAL DEFAULT 0
```

---

## 18. `hairgpu_final_db.py` の修正仕様

### 13.1 summary import追加

`import_runtime_run()` でsummaryから以下を読む。

```python
case_id = summary.get("case_id", "")
seed = int(summary.get("seed", 0))
settings_hash = summary.get("settings_hash", "")
warmup_frames = int(summary.get("warmup_frames", 0))
measurement_frames = int(summary.get("measurement_frames", 0))
p50_frame_ms = float(summary.get("p50_frame_ms", 0.0))
p99_frame_ms = float(summary.get("p99_frame_ms", 0.0))
```

feature flags：

```python
flags = summary.get("feature_flags", {})
enable_microtype = 1 if flags.get("enableMicrotype") else 0
enable_cbma = 1 if flags.get("enableCBMA") else 0
enable_physics_islands = 1 if flags.get("enablePhysicsIslands") else 0
enable_rod_repair = 1 if flags.get("enableRodRepair") else 0
enable_gpu_resident = 1 if flags.get("enableGpuResident") else 0
```

### 13.2 CSV import追加

`_pick_csv_value()` を使って、追加列を読み取る。

```python
int(self._pick_csv_value(row, "globalFrameIndex", "global_frame_index", default="0"))
int(self._pick_csv_value(row, "measuredFrameIndex", "measured_frame_index", default="0"))
int(self._pick_csv_value(row, "certificateFailureCount", "certificate_failure_count", default="0"))
float(self._pick_csv_value(row, "certificateFailureRate", "certificate_failure_rate", default="0"))
...
```

既存CSVにも対応するため、存在しない列はdefault値にする。

---

## 19. 650 manifest generator 初期実装コード

別添の `generate_experiment_650.py` を `tools/final_stage/generate_experiment_650.py` として追加する。

受け入れ条件：

```txt
1. デフォルト実行で650行のexperiments_650.jsonlを生成する。
2. runtime_suite_650.jsonのruns数も650である。
3. case_0001〜case_0650まで連番になる。
4. case_0641〜case_0650がBaseline_CurrentProjectになる。
5. Combo_11111が5 flagすべてtrueになる。
6. settings_hashが各caseに入る。
```

実行例：

```bash
python tools/final_stage/generate_experiment_650.py \
  --out-jsonl results/exp_650/experiments_650.jsonl \
  --out-suite results/exp_650/runtime_suite_650.json \
  --viewer-exe bin/Debug/HairSystem/HairSystem.exe \
  --asset default
```

---

## 20. 実行コマンド

manifest生成：

```bash
python tools/final_stage/generate_experiment_650.py \
  --out-jsonl results/exp_650/experiments_650.jsonl \
  --out-suite results/exp_650/runtime_suite_650.json
```

650実験実行：

```bash
python tools/final_stage/hairgpu_runtime_suite.py \
  --config results/exp_650/runtime_suite_650.json \
  --resume \
  --fail-policy continue
```

一部だけ再実行：

```bash
python tools/final_stage/hairgpu_runtime_suite.py \
  --config results/exp_650/runtime_suite_650.json \
  --resume \
  --fail-policy continue \
  --scenario-filter S05_ContactHeavy_Sphere
```

---

## 21. 受け入れ条件

### 21.1 C++ viewer

```txt
1. HAIRGPU_AUTO_TELEMETRY=1で既存どおりsummary/csvを出す。
2. HAIRGPU_EXPERIMENT_CASE_ID等が未指定でも通常viewerとして動く。
3. HAIRGPU_WARMUP_FRAMES=120, HAIRGPU_MEASUREMENT_FRAMES=600の場合、CSV/JSONLには600 frameだけ出る。
4. HAIRGPU_AUTO_EXIT_AFTER_CAPTURE=1の場合、720 frame実行後に自動終了する。
5. summary.jsonにp50/p95/p99が出る。
6. frames.jsonlが出る。
7. 新規性未実装でもfeature flagsと新規ログ列が0/nullで出る。
```

### 21.2 Python

```txt
1. generate_experiment_650.pyのデフォルト出力が650件になる。
2. runtime_suite_650.jsonをhairgpu_runtime_suite.pyが読める。
3. --resume指定時、summary/csvが存在するcaseをskipする。
4. --fail-policy continue指定時、失敗caseでerror.jsonを書き、次caseへ進む。
5. SQLiteにp99_frame_msとcase_idが保存される。
6. runtime_suite_outputsに論文用CSVが出る。
```

---

## 22. 実装作業指示

```txt
C++17 / Python 3.11で、必ず既存挙動を壊さない小さな差分として実装する。
添付されたHairSystemプロジェクトに対して、新規性5要素の内部実装は行わず、650パターン自動実験のための周辺基盤だけを実装してください。

対象ファイル:
- apps/dx11_viewer/src/GpuHairViewer.h
- apps/dx11_viewer/src/GpuHairViewer.cpp
- tools/final_stage/hairgpu_final_run.py
- tools/final_stage/hairgpu_runtime_suite.py
- tools/final_stage/hairgpu_final_db.py
- tools/final_stage/schema.sql

追加ファイル:
- tools/final_stage/generate_experiment_650.py

実装要件:
1. GpuHairViewerは環境変数からcase_id, variant, scenario, seed, warmup_frames, measurement_frames, feature flagsを読み取る。
2. warmup frameはtelemetryへ出さず、measurement frameだけCSV/JSONL/summaryへ出す。
3. HAIRGPU_AUTO_EXIT_AFTER_CAPTURE=1ならmeasurement終了後にviewerを終了する。
4. ExportPerformanceCsvは既存列を消さず、新規列を末尾追加する。
5. ExportPerformanceJsonlを追加し、1 frame 1 JSONとして出力する。
6. ExportRuntimeReportはp50/p95/p99、case情報、feature flags、frames.jsonl pathをsummaryに含める。
7. generate_experiment_650.pyはデフォルトで650件のcaseを生成する。
8. hairgpu_runtime_suite.pyは--resume, --fail-policy continue, --case-filter, --variant-filter, --scenario-filterに対応する。
9. hairgpu_final_db.pyとschema.sqlはcase_id, seed, p99, feature flags, certificate/fallback系列を保存できる。
10. 新規性未実装の値は0またはnullでよい。

禁止事項:
- 新規性5要素の中身を仮実装しない。
- 既存CSV列を削除しない。
- HAIRGPU_EXPERIMENT_CASE_ID未指定時の通常viewer挙動を壊さない。
- 650 caseの途中失敗で全体を止めない。--fail-policy continue時はerror.jsonへ記録する。

完了条件:
- `python tools/final_stage/generate_experiment_650.py` で650件のmanifestが生成される。
- `python tools/final_stage/hairgpu_runtime_suite.py --config results/exp_650/runtime_suite_650.json --resume --fail-policy continue` がcaseを順番に処理できる。
- summary.json, frames.csv, frames.jsonl, settings.json, error.jsonが仕様通り生成される。
```
