#!/usr/bin/env python3
"""Static fail-fast guard for the autonomous QA hardening overlay."""
from __future__ import annotations
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
errors: list[str] = []

def read(rel: str) -> str:
    p = ROOT / rel
    if not p.exists():
        errors.append(f"missing: {rel}")
        return ""
    return p.read_text(encoding="utf-8")

sandbox_targets = {
    "src/Dip.Infrastructure/P2Intelligence/FuzzExperimentRunner.cs": "RunQaAsync(",
    "src/Dip.Infrastructure/P2Intelligence/MutationExperimentRunner.cs": "RunQaAsync(",
    "src/Dip.Infrastructure/P2Intelligence/RepeatedExperimentRunner.cs": "RunQaAsync(",
    "src/Dip.Infrastructure/P2Intelligence/SoakExperimentRunner.cs": "RunQaObservedAsync(",
    "src/Dip.Infrastructure/Benchmarks/BenchmarkExecutor.cs": "RunQaAsync(",
    "src/Dip.Infrastructure/Replay/GameReplayExecutor.cs": "RunQaAsync(",
    "src/Dip.Infrastructure/P2Intelligence/MediaRegressionRunner.cs": "RunQaAsync(",
}
for rel, required in sandbox_targets.items():
    text = read(rel)
    if "Process.Start(" in text or "new ProcessStartInfo" in text:
        errors.append(f"direct untrusted process launch bypass: {rel}")
    if required not in text:
        errors.append(f"missing sandbox execution '{required}': {rel}")

checks = [
    ("src/Dip.Application/Features/QualityAutomation/AutonomousQaEvidenceValidator.cs", "Runner reported PASS but required evidence was missing", "required-evidence fail closed"),
    ("src/Dip.Application/Features/QualityAutomation/AutonomousQaCapabilityPolicy.cs", "AcceptedCapabilities", "experiment-kind capability policy"),
    ("src/Dip.Infrastructure/QualityAutomation/AutonomousQaExperimentDispatcher.cs", "AutonomousQaCapabilityPolicy.IsSatisfied", "capability-specific dispatcher gate"),
    ("src/Dip.Infrastructure/QualityAutomation/AutonomousQaExperimentDispatcher.cs", "ArtifactService artifacts", "durable artifact service"),
    ("src/Dip.Infrastructure/QualityAutomation/AutonomousQaExperimentDispatcher.cs", '"autonomous-qa-result"', "durable per-experiment result envelope"),
    ("src/Dip.Infrastructure/QualityAutomation/VerifiedFixService.cs", "_evidenceValidator.Validate", "verified-fix required evidence gate"),
    ("src/Dip.Infrastructure/QualityAutomation/VerifiedFixService.cs", "BaselineRevisionMismatches", "baseline revision gate"),
    ("src/Dip.Infrastructure/QualityAutomation/GitAutonomousQaWorkspaceManager.cs", '"worktree", "add", "--detach"', "exact-revision detached worktree"),
    ("src/Dip.Application/Features/QualityAutomation/AutonomousDebugService.cs", "policyRequiredKinds", "deterministic policy union"),
    ("src/Dip.Application/Features/QualityAutomation/AutonomousDebugService.cs", "adaptiveQaPlanner.PlanAsync", "adaptive planner authority"),
    ("src/Dip.Worker/Workers/AutonomousQaJobProgressReporter.cs", "job.Heartbeat", "durable progress heartbeat"),
    ("src/Dip.Web/Components/QA/QaDebugPanel.razor", "ProgressPercent", "progress UI"),
    ("src/Dip.Infrastructure/QualityAutomation/ExternalGpuCaptureQaPack.cs", "RunQaAsync", "sandboxed GPU capture"),
    ("src/Dip.Infrastructure/QualityAutomation/QaCapabilityPlanner.cs", "QaCapability.MemorySafety", "dedicated memory-safety capability"),
    ("src/Dip.Infrastructure/QualityAutomation/QaCapabilityPlanner.cs", "QaCapability.ShaderValidation", "dedicated shader-validation capability"),
    ("collectors/unity-collector/Packages/com.mkyosei.dip.collector/Editor/Analysis/RendererPerformanceAnalyzer.cs", "renderer_count", "Unity renderer depth"),
]
for rel, needle, label in checks:
    if needle not in read(rel):
        errors.append(f"missing invariant: {label} ({rel})")

manifest = ROOT / "samples/autonomous-qa-bug-zoo/manifest.json"
if not manifest.exists():
    errors.append("missing autonomous QA Bug Zoo manifest")
else:
    import json
    try:
        doc = json.loads(manifest.read_text(encoding="utf-8"))
        cases = doc.get("cases", [])
        if len(cases) < 15:
            errors.append(f"Bug Zoo too small: {len(cases)} cases; expected >= 15")
    except Exception as ex:
        errors.append(f"invalid Bug Zoo manifest: {ex}")


# Required-experiment selection must have a deterministic non-AI floor even when change metadata is missing.
policy = read("src/Dip.Application/Features/QualityAutomation/AutonomousQaDeterministicPolicy.cs")
if not ("VisualRegression" in policy and "RuntimeReplay" in policy and "Benchmark" in policy):
    errors.append("missing deterministic game/media baseline")
if not ("LoadTest" in policy and "Fuzz" in policy):
    errors.append("missing deterministic backend/service baseline")
service = read("src/Dip.Application/Features/QualityAutomation/AutonomousDebugService.cs")
if not ("AutonomousQaDeterministicPolicy.RequiredFor" in service and "policyRequired.Add" in service):
    errors.append("AutonomousDebug does not union deterministic policy")

if errors:
    print("AUTONOMOUS_QA_HARDENING=FAIL")
    for error in errors:
        print("- " + error)
    raise SystemExit(1)
print("AUTONOMOUS_QA_HARDENING=PASS")
