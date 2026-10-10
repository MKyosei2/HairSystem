// Purpose: Makes the autonomous-QA hardening contract explicit and observable across worker, evidence, and UI boundaries.
namespace Dip.Application.Features.QualityAutomation;

/// <summary>
/// Identifies the hardening revision that introduced mandatory sandbox execution,
/// exact-revision baselines, durable experiment artifacts, strict capability floors,
/// and required-evidence verification for autonomous QA.
/// </summary>
public static class AutonomousQaHardeningRevision
{
    public const string Current = "2026.10.05-r3";

    public static IReadOnlyList<string> Guarantees { get; } =
    [
        "sandboxed-runner-execution",
        "required-evidence-validation",
        "strict-capability-dispatch",
        "deterministic-required-experiments",
        "durable-artifact-persistence",
        "exact-base-commit-baseline",
        "durable-job-progress",
        "gpu-qa-extension"
    ];
}
