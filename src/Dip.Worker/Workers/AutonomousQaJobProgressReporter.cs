// Purpose: Publishes fine-grained autonomous-QA stages through the existing durable-job lease/progress fields.
using Dip.Application.Abstractions.Persistence;
using Dip.Application.Features.Jobs;
using Dip.Application.Features.QualityAutomation;
using Dip.Domain.Entities;
using Microsoft.Extensions.Options;

namespace Dip.Worker.Workers;

public sealed class AutonomousQaJobProgressReporter(
    IUnitOfWork uow,
    IOptions<DurableJobQueueOptions> options) : IAutonomousQaProgressReporter
{
    private DurableJob? _job;

    public void Bind(DurableJob job)
    {
        ArgumentNullException.ThrowIfNull(job);
        _job = job;
    }

    public async Task ReportAsync(decimal progressPercent, string stage, CancellationToken ct)
    {
        var job = _job ?? throw new InvalidOperationException("Autonomous QA progress reporter is not bound to a durable job.");
        if (string.IsNullOrWhiteSpace(job.LeaseOwner))
            throw new InvalidOperationException("Autonomous QA job does not have an active lease owner.");

        var lease = TimeSpan.FromSeconds(Math.Clamp(options.Value.LeaseSeconds, 15, 3600));
        job.Heartbeat(job.LeaseOwner, DateTimeOffset.UtcNow, lease, Math.Clamp(progressPercent, 0m, 99.9m), stage);
        await uow.SaveChangesAsync(ct);
    }
}
