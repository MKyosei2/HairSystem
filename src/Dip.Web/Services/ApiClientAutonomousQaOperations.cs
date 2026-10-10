// Purpose: Provides asynchronous autonomous-QA enqueue and session polling operations for the durable 202 Accepted API contract.
using System.Net;
using System.Net.Http.Json;
using Dip.Application.Features.QualityAutomation;
using Dip.Application.Features.Jobs;

namespace Dip.Web.Services;

public static class ApiClientAutonomousQaOperations
{
    public static async Task<AutonomousDebugEnqueuedDto> EnqueueAutonomousDebugAsync(
        this ApiClient client,
        Guid projectId,
        string subjectKey,
        string question,
        string evidenceJson,
        string workspaceRelativePath,
        string? repositoryContext = null,
        string? baseCommit = null,
        string? language = null,
        string domainId = "software",
        AutonomousDebugBudget? budget = null,
        string? experimentConfigurationJson = null,
        CancellationToken cancellationToken = default)
    {
        using var response = await client.Transport.PostAsJsonAsync(
            $"api/v1/defect-intelligence/projects/{projectId}/autonomous-debug",
            new
            {
                subjectKey,
                question,
                evidenceJson,
                workspaceRelativePath,
                repositoryContext,
                baseCommit,
                language,
                domainId,
                budget,
                experimentConfigurationJson
            },
            cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<AutonomousDebugEnqueuedDto>(
                   cancellationToken: cancellationToken)
               ?? throw new InvalidDataException("Autonomous QA enqueue API returned an empty response.");
    }

    public static async Task<AutonomousDebugSessionDto?> GetAutonomousDebugSessionAsync(
        this ApiClient client,
        Guid projectId,
        Guid sessionId,
        CancellationToken cancellationToken = default)
    {
        using var response = await client.Transport.GetAsync(
            $"api/v1/defect-intelligence/projects/{projectId}/autonomous-debug/{sessionId}",
            cancellationToken);
        if (response.StatusCode == HttpStatusCode.NotFound)
            return null;
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<AutonomousDebugSessionDto>(
            cancellationToken: cancellationToken);
    }
    public static async Task<DurableJobDto?> GetDurableJobAsync(
        this ApiClient client,
        Guid jobId,
        CancellationToken cancellationToken = default)
    {
        using var response = await client.Transport.GetAsync($"api/v1/jobs/{jobId}", cancellationToken);
        if (response.StatusCode == HttpStatusCode.NotFound)
            return null;
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<DurableJobDto>(cancellationToken: cancellationToken);
    }

}
