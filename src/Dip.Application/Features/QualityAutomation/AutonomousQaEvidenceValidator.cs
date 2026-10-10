// Purpose: Enforces that a PASS has the exact evidence kinds required by its formal autonomous-QA contract.
namespace Dip.Application.Features.QualityAutomation;

public sealed class AutonomousQaEvidenceValidator
{
    public AutonomousQaEvidenceValidation Validate(
        AutonomousQaExperimentStep step,
        IReadOnlyList<string>? evidenceReferences)
    {
        ArgumentNullException.ThrowIfNull(step);
        var references = (evidenceReferences ?? Array.Empty<string>())
            .Where(x => !string.IsNullOrWhiteSpace(x))
            .Select(x => x.Trim())
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        var missing = new List<string>();
        var matched = new List<string>();
        foreach (var requirement in step.RequiredEvidence.Where(x => x.Required))
        {
            var prefix = requirement.Kind.Trim() + ":";
            var hits = references
                .Where(x => x.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                .ToArray();
            if (hits.Length == 0)
                missing.Add(requirement.Kind);
            else
                matched.AddRange(hits);
        }

        return new(
            missing.Count == 0,
            missing.Distinct(StringComparer.OrdinalIgnoreCase).ToArray(),
            matched.Distinct(StringComparer.OrdinalIgnoreCase).ToArray());
    }

    public AutonomousQaExperimentResult Enforce(
        AutonomousQaExperimentStep step,
        AutonomousQaExperimentResult result)
    {
        var validation = Validate(step, result.EvidenceReferences);
        if (result.Status != AutonomousQaExperimentStatus.Passed)
            return result with { Verified = false, EvidenceValidation = validation };
        if (validation.Satisfied)
            return result with { Verified = true, EvidenceValidation = validation };

        return result with
        {
            Status = AutonomousQaExperimentStatus.InfrastructureFailure,
            Verified = false,
            EvidenceValidation = validation,
            Summary =
                $"Runner reported PASS but required evidence was missing ({string.Join(", ", validation.MissingKinds)}). "
                + result.Summary
        };
    }
}
