using System.Linq;
using Dip.UnityCollector.Editor.Configuration;
using Dip.UnityCollector.Editor.Models;
using UnityEditor;
using UnityEngine;

namespace Dip.UnityCollector.Editor.Analysis
{
    public static class MaterialAnalyzer
    {
        public static void Analyze(CollectorReport report, UnityCollectorSettings settings)
        {
            var materialGuids = AssetDatabase.FindAssets("t:Material");
            var missingShader = 0; var unsupportedShader = 0; var keywordHeavy = 0;
            foreach (var guid in materialGuids)
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                var material = AssetDatabase.LoadAssetAtPath<Material>(path);
                if (material == null) continue;
                if (material.shader == null)
                {
                    missingShader++;
                    Add(report, "Critical", "Material has missing shader", "Material shader reference is null.", path);
                    continue;
                }
                if (!material.shader.isSupported)
                {
                    unsupportedShader++;
                    Add(report, "Warning", "Material shader is unsupported", material.shader.name, path);
                }
                var keywordCount = material.shaderKeywords == null ? 0 : material.shaderKeywords.Length;
                if (keywordCount > settings.maxShaderKeywords)
                {
                    keywordHeavy++;
                    Add(report, "Warning", "Material has many enabled shader keywords", $"{keywordCount} keywords exceeds {settings.maxShaderKeywords}.", path);
                }
            }

            foreach (var renderer in Object.FindObjectsByType<Renderer>(FindObjectsSortMode.None))
            {
                var materials = renderer.sharedMaterials;
                if (materials.Length > settings.maxMaterialsPerRenderer)
                    Add(report, "Warning", "Too many materials on renderer", $"{materials.Length} materials are assigned.", renderer.gameObject.scene.path);
                if (materials.Any(x => x == null))
                    Add(report, "Critical", "Renderer has missing material", renderer.name, renderer.gameObject.scene.path);
            }

            Metric(report, "material_count", materialGuids.Length, "count");
            Metric(report, "material_missing_shader_count", missingShader, "count");
            Metric(report, "material_unsupported_shader_count", unsupportedShader, "count");
            Metric(report, "material_high_keyword_count", keywordHeavy, "count");
        }

        public static void Analyze(CollectorReport report, int maxMaterialsPerRenderer)
        {
            var settings = UnityCollectorSettings.LoadOrCreate();
            settings.maxMaterialsPerRenderer = maxMaterialsPerRenderer;
            Analyze(report, settings);
        }

        private static void Add(CollectorReport report, string severity, string title, string description, string source) =>
            report.issues.Add(new CollectorIssue { category = "asset", severity = severity, title = title, description = description, source = source });
        private static void Metric(CollectorReport report, string name, double value, string unit) =>
            report.metrics.Add(new CollectorMetric { name = name, value = value, unit = unit });
    }
}
