using Dip.UnityCollector.Editor.Configuration;
using Dip.UnityCollector.Editor.Models;
using UnityEditor;
using UnityEngine;
using UnityEngine.Profiling;

namespace Dip.UnityCollector.Editor.Analysis
{
    public static class TextureAnalyzer
    {
        public static void Analyze(CollectorReport report, UnityCollectorSettings settings)
        {
            var guids = AssetDatabase.FindAssets("t:Texture");
            long runtimeBytes = 0;
            var readable = 0;
            var noMip = 0;
            var uncompressedLarge = 0;
            var normalMapSrgb = 0;

            foreach (var guid in guids)
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                var texture = AssetDatabase.LoadAssetAtPath<Texture>(path);
                if (texture == null) continue;
                runtimeBytes += System.Math.Max(0L, Profiler.GetRuntimeMemorySizeLong(texture));

                var importer = AssetImporter.GetAtPath(path) as TextureImporter;
                if (texture.width > settings.maxTextureSize || texture.height > settings.maxTextureSize)
                    Add(report, "Warning", "Oversized texture", $"{texture.width}x{texture.height} exceeds {settings.maxTextureSize}.", path);

                if (importer == null) continue;
                if (importer.isReadable)
                {
                    readable++;
                    if (settings.warnReadableTextures)
                        Add(report, "Warning", "Readable texture duplicates CPU-side memory", "Read/Write is enabled. Disable it unless runtime CPU pixel access is required.", path);
                }
                if (!importer.mipmapEnabled && System.Math.Max(texture.width, texture.height) >= settings.requireMipmapsAboveSize)
                {
                    noMip++;
                    Add(report, "Warning", "Large texture has mipmaps disabled", $"Texture is >= {settings.requireMipmapsAboveSize}px but mipmaps are disabled.", path);
                }
                if (importer.textureCompression == TextureImporterCompression.Uncompressed
                    && System.Math.Max(texture.width, texture.height) >= settings.warnUncompressedTextureAboveSize)
                {
                    uncompressedLarge++;
                    Add(report, "Warning", "Large texture is uncompressed", "Importer textureCompression is Uncompressed; verify platform memory/bandwidth budget.", path);
                }
                if (importer.textureType == TextureImporterType.NormalMap && importer.sRGBTexture)
                {
                    normalMapSrgb++;
                    Add(report, "Warning", "Normal map uses sRGB sampling", "Normal maps should normally be imported as linear data.", path);
                }
            }

            Metric(report, "texture_count", guids.Length, "count");
            Metric(report, "estimated_texture_runtime_memory_mb", runtimeBytes / 1024d / 1024d, "MB");
            Metric(report, "readable_texture_count", readable, "count");
            Metric(report, "large_texture_without_mipmap_count", noMip, "count");
            Metric(report, "large_uncompressed_texture_count", uncompressedLarge, "count");
            Metric(report, "normal_map_srgb_count", normalMapSrgb, "count");
        }

        // Compatibility overload for callers compiled against collector <= 0.4.
        public static void Analyze(CollectorReport report, int maxTextureSize)
        {
            var settings = UnityCollectorSettings.LoadOrCreate();
            settings.maxTextureSize = maxTextureSize;
            Analyze(report, settings);
        }

        private static void Add(CollectorReport report, string severity, string title, string description, string source) =>
            report.issues.Add(new CollectorIssue { category = "asset", severity = severity, title = title, description = description, source = source });

        private static void Metric(CollectorReport report, string name, double value, string unit) =>
            report.metrics.Add(new CollectorMetric { name = name, value = value, unit = unit });
    }
}
