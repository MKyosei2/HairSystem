// Purpose: Defines a non-AI minimum autonomous-QA experiment set from changed paths, domain and language.
using Dip.Domain.ValueObjects;

namespace Dip.Application.Features.QualityAutomation;

/// <summary>
/// Mandatory autonomous-QA policy. AI hypotheses may add experiments, but they
/// cannot remove experiments selected here. When change metadata is unavailable,
/// a conservative domain/language baseline is returned instead of falling back to
/// AI-only selection.
/// </summary>
public static class AutonomousQaDeterministicPolicy
{
    public static IReadOnlyList<DebugExperimentKind> RequiredFor(
        IReadOnlyList<string>? changedFiles,
        string? domainId,
        LanguageId language)
    {
        var files = (changedFiles ?? Array.Empty<string>())
            .Where(x => !string.IsNullOrWhiteSpace(x))
            .Select(Normalize)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToArray();

        var required = new HashSet<DebugExperimentKind>();
        var domain = (domainId ?? string.Empty).Trim().ToLowerInvariant();

        if (files.Length == 0)
        {
            // No trusted change metadata is available. Choose a conservative
            // deterministic floor from domain/language rather than delegating the
            // minimum verification boundary to the AI hypothesis text.
            required.Add(DebugExperimentKind.StaticAnalysis);
            required.Add(DebugExperimentKind.RegressionTest);

            if (IsNative(language))
            {
                required.Add(DebugExperimentKind.Compile);
                required.Add(DebugExperimentKind.MemorySafety);
            }

            if (ContainsAny(domain, "game", "media", "render", "graphics", "unity", "unreal", "dcc"))
            {
                required.Add(DebugExperimentKind.RuntimeReplay);
                required.Add(DebugExperimentKind.VisualRegression);
                required.Add(DebugExperimentKind.Benchmark);
            }

            if (ContainsAny(domain, "service", "server", "backend", "api", "network", "distributed"))
            {
                required.Add(DebugExperimentKind.Fuzz);
                required.Add(DebugExperimentKind.LoadTest);
            }

            return Order(required);
        }

        var docsOnly = files.All(IsDocumentation);
        if (docsOnly)
        {
            required.Add(DebugExperimentKind.StaticAnalysis);
            return Order(required);
        }

        required.Add(DebugExperimentKind.Compile);
        required.Add(DebugExperimentKind.StaticAnalysis);
        required.Add(DebugExperimentKind.RegressionTest);

        if (IsNative(language) || files.Any(IsNativePath))
            required.Add(DebugExperimentKind.MemorySafety);

        if (files.Any(IsRenderingPath))
        {
            required.Add(DebugExperimentKind.ShaderValidation);
            required.Add(DebugExperimentKind.RuntimeReplay);
            required.Add(DebugExperimentKind.VisualRegression);
            required.Add(DebugExperimentKind.Benchmark);
        }

        if (files.Any(IsAssetPath))
            required.Add(DebugExperimentKind.AssetValidation);

        if (files.Any(IsAudioPath))
            required.Add(DebugExperimentKind.AudioRegression);

        if (files.Any(IsNetworkPath))
        {
            required.Add(DebugExperimentKind.Fuzz);
            required.Add(DebugExperimentKind.LoadTest);
        }

        if (files.Any(IsConcurrencyPath))
        {
            required.Add(DebugExperimentKind.RaceDetection);
            required.Add(DebugExperimentKind.Stress);
        }

        if (files.Any(IsMemorySensitivePath))
            required.Add(DebugExperimentKind.Soak);

        return Order(required);
    }

    private static IReadOnlyList<DebugExperimentKind> Order(HashSet<DebugExperimentKind> values) =>
        values.OrderBy(x => (int)x).ToArray();

    private static bool IsNative(LanguageId language) =>
        language == LanguageId.Cpp || language == LanguageId.C || language == LanguageId.ObjectiveC;

    private static bool IsDocumentation(string path)
    {
        var ext = Path.GetExtension(path);
        return path.StartsWith("docs/", StringComparison.OrdinalIgnoreCase)
            || ext.Equals(".md", StringComparison.OrdinalIgnoreCase)
            || ext.Equals(".txt", StringComparison.OrdinalIgnoreCase)
            || ext.Equals(".rst", StringComparison.OrdinalIgnoreCase)
            || ext.Equals(".adoc", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsNativePath(string p) => EndsWithAny(p, ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".m", ".mm");
    private static bool IsRenderingPath(string p) => ContainsAny(p, "render", "shader", "material", "texture", "lighting", "camera", "scene", "vfx", "particle", "ui/") || EndsWithAny(p, ".hlsl", ".glsl", ".shader", ".compute");
    private static bool IsAssetPath(string p) => ContainsAny(p, "assets/", "art/", "prefab", "mesh", "model", "maya", "blender", "fbx", "animation", "texture", "material") || EndsWithAny(p, ".ma", ".mb", ".fbx", ".blend", ".obj", ".usd", ".usda", ".usdc", ".prefab", ".asset");
    private static bool IsAudioPath(string p) => ContainsAny(p, "audio", "sound", "music", "bgm", "voice", "se/") || EndsWithAny(p, ".wav", ".mp3", ".ogg", ".flac");
    private static bool IsNetworkPath(string p) => ContainsAny(p, "network", "socket", "packet", "protocol", "serialize", "api/", "server", "http", "grpc", "websocket");
    private static bool IsConcurrencyPath(string p) => ContainsAny(p, "thread", "concurrent", "parallel", "async", "lock", "mutex", "semaphore", "job-system", "jobsystem");
    private static bool IsMemorySensitivePath(string p) => ContainsAny(p, "memory", "allocator", "pool", "cache", "streaming", "native", "unsafe") || IsNativePath(p);

    private static string Normalize(string value) => value.Replace('\\', '/').Trim().ToLowerInvariant();
    private static bool ContainsAny(string value, params string[] tokens) => tokens.Any(x => value.Contains(x, StringComparison.OrdinalIgnoreCase));
    private static bool EndsWithAny(string value, params string[] suffixes) => suffixes.Any(x => value.EndsWith(x, StringComparison.OrdinalIgnoreCase));
}
