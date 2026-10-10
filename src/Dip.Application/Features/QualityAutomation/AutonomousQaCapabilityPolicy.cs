// Purpose: Defines strict capability floors for formal autonomous-QA experiments.
namespace Dip.Application.Features.QualityAutomation;

public static class AutonomousQaCapabilityPolicy
{
    /// <summary>
    /// Returns the capabilities that are acceptable evidence for an experiment.
    /// Some experiments accept alternatives (for example Compile or TypeCheck),
    /// while high-risk domains such as shader, asset and memory-safety require
    /// their dedicated capability and cannot be satisfied by a generic build.
    /// </summary>
    public static IReadOnlySet<QaCapability> AcceptedCapabilities(DebugExperimentKind kind) =>
        kind switch
        {
            DebugExperimentKind.Compile => Set(QaCapability.Compile, QaCapability.TypeCheck),
            DebugExperimentKind.StaticAnalysis => Set(QaCapability.StaticAnalysis, QaCapability.AstAnalysis, QaCapability.DataFlowAnalysis),
            DebugExperimentKind.UnitTest or DebugExperimentKind.RegressionTest => Set(QaCapability.UnitTest),
            DebugExperimentKind.AssetValidation => Set(QaCapability.AssetValidation),
            DebugExperimentKind.ShaderValidation => Set(QaCapability.ShaderValidation),
            DebugExperimentKind.Reproduce => Set(QaCapability.Runtime),
            DebugExperimentKind.MemorySafety => Set(QaCapability.MemorySafety),
            DebugExperimentKind.RaceDetection => Set(QaCapability.RaceDetection),
            DebugExperimentKind.DeadlockDetection => Set(QaCapability.DeadlockDetection),
            DebugExperimentKind.Security => Set(QaCapability.Security),
            _ => new HashSet<QaCapability>()
        };

    public static bool IsSatisfied(
        DebugExperimentKind kind,
        IEnumerable<QaCapability> executedCapabilities)
    {
        var accepted = AcceptedCapabilities(kind);
        if (accepted.Count == 0)
            return true;
        var executed = executedCapabilities.ToHashSet();
        return accepted.Any(executed.Contains);
    }

    public static string RequirementDescription(DebugExperimentKind kind)
    {
        var accepted = AcceptedCapabilities(kind);
        return accepted.Count == 0
            ? "No capability floor is defined."
            : "Requires one of: " + string.Join(", ", accepted.OrderBy(x => x));
    }

    public static string EvidenceKind(DebugExperimentKind kind) =>
        kind switch
        {
            DebugExperimentKind.Compile => "tool-execution",
            DebugExperimentKind.StaticAnalysis => "finding-set",
            DebugExperimentKind.UnitTest or DebugExperimentKind.RegressionTest => "test-result",
            DebugExperimentKind.AssetValidation => "asset-qa",
            DebugExperimentKind.ShaderValidation => "shader-qa",
            DebugExperimentKind.Reproduce => "reproduction",
            DebugExperimentKind.MemorySafety => "memory-safety",
            DebugExperimentKind.RaceDetection or DebugExperimentKind.DeadlockDetection => "concurrency",
            DebugExperimentKind.Security => "security",
            _ => "experiment"
        };

    private static IReadOnlySet<QaCapability> Set(params QaCapability[] capabilities) =>
        new HashSet<QaCapability>(capabilities);
}
