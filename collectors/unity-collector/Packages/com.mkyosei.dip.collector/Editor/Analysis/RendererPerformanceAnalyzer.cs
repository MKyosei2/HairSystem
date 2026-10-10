using System.Linq;
using Dip.UnityCollector.Editor.Configuration;
using Dip.UnityCollector.Editor.Models;
using UnityEngine;
using UnityEngine.Rendering;

namespace Dip.UnityCollector.Editor.Analysis
{
    public static class RendererPerformanceAnalyzer
    {
        public static void Analyze(CollectorReport report, UnityCollectorSettings settings)
        {
            var renderers = Object.FindObjectsByType<Renderer>(FindObjectsSortMode.None);
            var skinned = 0; var transparent = 0; var shadow = 0; long materialSlots = 0;
            foreach (var renderer in renderers)
            {
                if (renderer is SkinnedMeshRenderer) skinned++;
                if (renderer.shadowCastingMode != ShadowCastingMode.Off) shadow++;
                materialSlots += renderer.sharedMaterials.Length;
                if (renderer.sharedMaterials.Any(m => m != null && m.renderQueue >= 3000)) transparent++;
            }
            var particles = Object.FindObjectsByType<ParticleSystem>(FindObjectsSortMode.None).Length;

            Gate(report, renderers.Length, settings.maxRenderers, "Renderer count exceeds configured envelope", "renderers");
            Gate(report, skinned, settings.maxSkinnedMeshRenderers, "SkinnedMeshRenderer count exceeds configured envelope", "skinned renderers");
            Gate(report, transparent, settings.maxTransparentRenderers, "Transparent renderer pressure exceeds configured envelope", "transparent renderers");
            Gate(report, shadow, settings.maxShadowCastingRenderers, "Shadow-casting renderer pressure exceeds configured envelope", "shadow-casting renderers");
            Gate(report, particles, settings.maxParticleSystems, "Particle system count exceeds configured envelope", "particle systems");

            Metric(report, "renderer_count", renderers.Length, "count");
            Metric(report, "skinned_mesh_renderer_count", skinned, "count");
            Metric(report, "transparent_renderer_count", transparent, "count");
            Metric(report, "shadow_casting_renderer_count", shadow, "count");
            Metric(report, "renderer_material_slot_count", materialSlots, "count");
            Metric(report, "particle_system_count", particles, "count");
        }

        private static void Gate(CollectorReport report, int value, int limit, string title, string noun)
        {
            if (limit > 0 && value > limit)
                report.issues.Add(new CollectorIssue { category = "performance", severity = "Warning", title = title, description = $"{value} {noun}; configured limit is {limit}.", source = "Unity Scene" });
        }
        private static void Metric(CollectorReport report, string name, double value, string unit) =>
            report.metrics.Add(new CollectorMetric { name = name, value = value, unit = unit });
    }
}
