using System;
using System.Globalization;
using System.IO;
using System.Linq;
using Dip.UnityCollector.Editor.Analysis;
using Dip.UnityCollector.Editor.Configuration;
using Dip.UnityCollector.Editor.Models;
using Dip.UnityCollector.Editor.Reporting;
using Dip.UnityCollector.Runtime;
using UnityEditor;
using UnityEngine;

namespace Dip.UnityCollector.Editor.Cli
{
    public static class UnityCollectorCli
    {
        private const string CollectorVersion = "0.5.0";

        public static void Run()
        {
            var started = DateTimeOffset.UtcNow;
            var output = GetArg("-dipOutput");
            var report = new CollectorReport
            {
                collectorVersion = CollectorVersion,
                projectId = GetArg("-dipProjectId"),
                commitSha = GetArg("-dipCommitSha"),
                branch = GetArg("-dipBranch"),
                startedAt = started.ToString("O")
            };

            var exitCode = 0;
            try
            {
                var settings = UnityCollectorSettings.LoadOrCreate();
                AnalyzeProject(report, settings, GetOptionalArg("-dipRuntimeMetrics"));

                var qualityGate = NormalizeQualityGate(GetOptionalArg("-dipQualityGate"));
                AddIssueSummaryMetrics(report);
                var gateFailed = ShouldFailQualityGate(report, qualityGate);
                var hasCritical = report.issues.Exists(issue =>
                    string.Equals(issue.severity, "Critical", StringComparison.OrdinalIgnoreCase));

                report.status = gateFailed ? "failed" : hasCritical ? "warning" : "succeeded";
                if (gateFailed)
                {
                    exitCode = 2;
                    Debug.LogError($"DIP Unity quality gate '{qualityGate}' failed. See the generated collector report for actionable findings.");
                }
            }
            catch (Exception ex)
            {
                exitCode = 1;
                report.status = "failed";
                report.issues.Add(new CollectorIssue
                {
                    category = "collector",
                    severity = "Critical",
                    title = "Unity collector failed",
                    description = ex.ToString(),
                    source = "UnityCollectorCli"
                });
                Debug.LogException(ex);
            }
            finally
            {
                report.finishedAt = DateTimeOffset.UtcNow.ToString("O");
                UnityCollectorReportWriter.Write(output, report);
            }

            if (exitCode != 0) EditorApplication.Exit(exitCode);
        }

        public static void AnalyzeProject(CollectorReport report, UnityCollectorSettings settings, string runtimeMetricsPath = null)
        {
            if (report == null) throw new ArgumentNullException(nameof(report));
            if (settings == null) throw new ArgumentNullException(nameof(settings));

            TextureAnalyzer.Analyze(report, settings);
            MeshAnalyzer.Analyze(report, settings);
            MaterialAnalyzer.Analyze(report, settings);
            RendererPerformanceAnalyzer.Analyze(report, settings);
            BuildSettingsAnalyzer.Analyze(report);
            AnimationAnalyzer.Analyze(report, settings.maxAnimationCurvesPerClip);
            SceneAnalyzer.Analyze(report, settings.maxSceneGameObjects, settings.analyzeAllEnabledBuildScenes);
            AssetDependencyAnalyzer.Analyze(report, settings.maxDependencyFindings);
            ShaderAndRuntimeAnalyzer.Analyze(report, settings.maxScriptHeuristicWarnings);
            IngestRuntimePerformance(report, settings, runtimeMetricsPath);
        }

        public static bool ShouldFailQualityGate(CollectorReport report, string qualityGate)
        {
            if (report == null) throw new ArgumentNullException(nameof(report));
            var normalized = NormalizeQualityGate(qualityGate);
            if (normalized == "off") return false;
            var hasCritical = report.issues.Any(issue => string.Equals(issue.severity, "Critical", StringComparison.OrdinalIgnoreCase));
            if (hasCritical) return true;
            return normalized == "warning" && report.issues.Any(issue => string.Equals(issue.severity, "Warning", StringComparison.OrdinalIgnoreCase));
        }

        private static void AddIssueSummaryMetrics(CollectorReport report)
        {
            AddMetric(report, "collector_issue_critical_count", report.issues.Count(issue => string.Equals(issue.severity, "Critical", StringComparison.OrdinalIgnoreCase)), "count");
            AddMetric(report, "collector_issue_warning_count", report.issues.Count(issue => string.Equals(issue.severity, "Warning", StringComparison.OrdinalIgnoreCase)), "count");
        }

        private static string NormalizeQualityGate(string value)
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? "critical" : value.Trim().ToLowerInvariant();
            if (normalized == "off" || normalized == "critical" || normalized == "warning") return normalized;
            throw new ArgumentException($"Invalid -dipQualityGate value '{value}'. Expected off, critical, or warning.");
        }

        private static void IngestRuntimePerformance(CollectorReport report, UnityCollectorSettings settings, string explicitPath)
        {
            var path = string.IsNullOrWhiteSpace(explicitPath) ? settings.runtimePerformanceReportPath : explicitPath;
            if (string.IsNullOrWhiteSpace(path)) return;
            var fullPath = Path.IsPathRooted(path) ? path : Path.GetFullPath(path);
            if (!File.Exists(fullPath))
            {
                if (!string.IsNullOrWhiteSpace(explicitPath))
                    report.issues.Add(new CollectorIssue { category = "performance", severity = "Warning", title = "Requested runtime performance evidence is missing", description = $"The runtime probe report was not found: {fullPath}", source = fullPath });
                return;
            }

            PerformanceProbe.Snapshot snapshot;
            try { snapshot = JsonUtility.FromJson<PerformanceProbe.Snapshot>(File.ReadAllText(fullPath)); }
            catch (Exception ex)
            {
                report.issues.Add(new CollectorIssue { category = "performance", severity = "Warning", title = "Runtime performance evidence could not be parsed", description = ex.Message, source = fullPath });
                return;
            }
            if (snapshot == null || snapshot.sampleCount <= 0)
            {
                report.issues.Add(new CollectorIssue { category = "performance", severity = "Warning", title = "Runtime performance evidence has no samples", description = "PerformanceProbe produced no post-warmup frame samples.", source = fullPath });
                return;
            }

            AddMetric(report, "runtime_frame_samples", snapshot.sampleCount, "count");
            AddMetric(report, "frame_time_avg_ms", snapshot.averageFrameMs, "ms");
            AddMetric(report, "frame_time_p50_ms", snapshot.p50FrameMs, "ms");
            AddMetric(report, "frame_time_p95_ms", snapshot.p95FrameMs, "ms");
            AddMetric(report, "frame_time_p99_ms", snapshot.p99FrameMs, "ms");
            AddMetric(report, "frame_time_max_ms", snapshot.maxFrameMs, "ms");
            AddMetric(report, "mono_used_max_mb", snapshot.maxMonoUsedMb, "MB");
            AddMetric(report, "total_allocated_max_mb", snapshot.maxTotalAllocatedMb, "MB");
            AddMetric(report, "gc_gen0_collections", snapshot.gcCollectionDelta0, "count");
            AddMetric(report, "gc_gen1_collections", snapshot.gcCollectionDelta1, "count");
            AddMetric(report, "gc_gen2_collections", snapshot.gcCollectionDelta2, "count");

            if (snapshot.p95FrameMs >= settings.criticalFrameTimeP95Ms)
                AddPerformanceIssue(report, "Critical", "Critical p95 frame-time regression risk", $"p95 frame time is {snapshot.p95FrameMs.ToString("0.###", CultureInfo.InvariantCulture)} ms; configured critical threshold is {settings.criticalFrameTimeP95Ms:0.###} ms.", fullPath);
            else if (snapshot.p95FrameMs >= settings.warningFrameTimeP95Ms)
                AddPerformanceIssue(report, "Warning", "p95 frame time exceeds target", $"p95 frame time is {snapshot.p95FrameMs.ToString("0.###", CultureInfo.InvariantCulture)} ms; configured warning threshold is {settings.warningFrameTimeP95Ms:0.###} ms.", fullPath);

            if (snapshot.maxMonoUsedMb >= settings.warningMonoMemoryMb)
                AddPerformanceIssue(report, "Warning", "Managed memory exceeds configured envelope", $"Maximum Mono used memory is {snapshot.maxMonoUsedMb:0.##} MB; configured threshold is {settings.warningMonoMemoryMb:0.##} MB.", fullPath);
        }

        private static void AddMetric(CollectorReport report, string name, double value, string unit) => report.metrics.Add(new CollectorMetric { name = name, value = value, unit = unit });
        private static void AddPerformanceIssue(CollectorReport report, string severity, string title, string description, string source) => report.issues.Add(new CollectorIssue { category = "performance", severity = severity, title = title, description = description, source = source });

        private static string GetArg(string name)
        {
            var value = GetOptionalArg(name);
            if (!string.IsNullOrWhiteSpace(value)) return value;
            throw new ArgumentException($"Missing required argument: {name}");
        }

        private static string GetOptionalArg(string name)
        {
            var args = Environment.GetCommandLineArgs();
            for (var i = 0; i < args.Length - 1; i++)
                if (args[i] == name) return args[i + 1];
            return null;
        }
    }
}
