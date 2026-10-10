// Purpose: Defines exact-revision autonomous-QA workspace and deterministic change-set boundaries.
namespace Dip.Application.Features.QualityAutomation;

public interface IAutonomousQaWorkspaceLease : IAsyncDisposable
{
    string WorkspacePath { get; }
    string Revision { get; }
}

public interface IAutonomousQaWorkspaceLeaseFactory
{
    Task<IAutonomousQaWorkspaceLease> CreateBaselineAsync(
        string workspaceRelativePath,
        string? revision,
        CancellationToken ct);
}

public interface IAutonomousQaChangeSetResolver
{
    Task<IReadOnlyList<string>> ResolveChangedFilesAsync(
        string workspaceRelativePath,
        string? revision,
        CancellationToken ct);
}
