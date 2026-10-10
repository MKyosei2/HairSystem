// Purpose: Composes and starts the DIP background worker host.
using Dip.Application;
using Dip.Application.Features.Jobs;
using Dip.Application.Features.QualityAutomation;
using Dip.Infrastructure;
using Dip.Infrastructure.QualityAutomation;
using Dip.Worker.Options;
using Dip.Worker.Workers;
using OpenTelemetry.Metrics;
using OpenTelemetry.Resources;
using OpenTelemetry.Trace;

var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddApplication();
builder.Services.AddInfrastructure(builder.Configuration);
builder.Services.Configure<WorkerOptions>(builder.Configuration.GetSection(WorkerOptions.SectionName));
builder.Services.Configure<DurableJobQueueOptions>(builder.Configuration.GetSection(DurableJobQueueOptions.SectionName));

// Production autonomous QA executes third-party code. Do not allow a production
// worker to start unless external network and filesystem isolation are explicitly
// required in addition to ProcessExecutor's in-process limits.
if (builder.Environment.IsProduction())
{
    var qa = builder.Configuration.GetSection(QaExecutionOptions.SectionName).Get<QaExecutionOptions>() ?? new QaExecutionOptions();
    if (!qa.SandboxEnabled || !qa.RequireExternalNetworkIsolation || !qa.RequireExternalFilesystemIsolation)
        throw new InvalidOperationException(
            "Production autonomous QA requires Qa:SandboxEnabled=true, Qa:RequireExternalNetworkIsolation=true, and Qa:RequireExternalFilesystemIsolation=true.");
}

// Autonomous QA executes only in the worker. Every experiment shares the same
// sandbox, exact-revision workspace and durable Artifact Store boundaries.
builder.Services.AddSingleton<AutonomousQaEvidenceValidator>();
builder.Services.AddSingleton<ICrossCuttingQaPack, ExternalGpuCaptureQaPack>();
builder.Services.AddSingleton<GitAutonomousQaWorkspaceManager>();
builder.Services.AddSingleton<IAutonomousQaWorkspaceLeaseFactory>(sp => sp.GetRequiredService<GitAutonomousQaWorkspaceManager>());
builder.Services.AddSingleton<IAutonomousQaChangeSetResolver>(sp => sp.GetRequiredService<GitAutonomousQaWorkspaceManager>());
builder.Services.AddScoped<IAutonomousQaExperimentDispatcher, AutonomousQaExperimentDispatcher>();
builder.Services.AddScoped<AutonomousQaJobProgressReporter>();
builder.Services.AddScoped<IAutonomousQaProgressReporter>(sp => sp.GetRequiredService<AutonomousQaJobProgressReporter>());
// Override the infrastructure compatibility registration in the worker so formal
// experiment verification can consume the scoped dispatcher/artifact pipeline.
builder.Services.AddScoped<IVerifiedFixService, VerifiedFixService>();

var otlpEndpointText = builder.Configuration["OpenTelemetry:OtlpEndpoint"];
Uri? otlpEndpoint = null;
if (!string.IsNullOrWhiteSpace(otlpEndpointText))
{
    if (!Uri.TryCreate(otlpEndpointText, UriKind.Absolute, out otlpEndpoint))
        throw new InvalidOperationException("OpenTelemetry:OtlpEndpoint must be a valid absolute URI.");
}

builder.Services.AddOpenTelemetry()
    .ConfigureResource(r => r.AddService("dip-worker"))
    .WithTracing(t =>
    {
        t.AddHttpClientInstrumentation()
            .AddSource("DIP.DevelopmentIntelligence");
        if (otlpEndpoint is not null)
            t.AddOtlpExporter(o => o.Endpoint = otlpEndpoint);
    })
    .WithMetrics(m =>
    {
        m.AddHttpClientInstrumentation()
            .AddRuntimeInstrumentation()
            .AddMeter("DIP.DevelopmentIntelligence");
        if (otlpEndpoint is not null)
            m.AddOtlpExporter(o => o.Endpoint = otlpEndpoint);
    });

builder.Services.AddScoped<IDurableJobHandler, FuzzJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, MutationJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, SoakJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, GitBisectJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, MediaRegressionJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, BenchmarkJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, GameSessionReplayJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, ReleaseEvidenceJobHandler>();
builder.Services.AddScoped<IDurableJobHandler, AutonomousQaSessionJobHandler>();

builder.Services.AddHostedService<OutboxWorker>();
builder.Services.AddHostedService<CollectorReportProcessedWorker>();
builder.Services.AddHostedService<DurableJobWorker>();
builder.Services.AddHostedService<DurableJobRecoveryWorker>();
builder.Services.AddHostedService<ArtifactRetentionWorker>();
builder.Services.AddHostedService<WorkerMetricsEndpoint>();
builder.Services.AddHostedService<AutomaticRegressionTriageWorker>();
builder.Services.AddHostedService<GitHubChangeRiskWorker>();
builder.Services.AddHostedService<ManagementNotificationWorker>();

var host = builder.Build();
await host.RunAsync();
