using Dip.UnityCollector.Editor.Configuration;
using Dip.UnityCollector.Editor.Models;
using UnityEditor;
using UnityEngine;

namespace Dip.UnityCollector.Editor.Analysis
{
    public static class MeshAnalyzer
    {
        public static void Analyze(CollectorReport report, UnityCollectorSettings settings)
        {
            var guids = AssetDatabase.FindAssets("t:Mesh");
            long totalTriangles = 0, totalVertices = 0, degenerate = 0;
            var missingUv = 0; var tooManySubMeshes = 0; var readable = 0;

            foreach (var guid in guids)
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                var mesh = AssetDatabase.LoadAssetAtPath<Mesh>(path);
                if (mesh == null) continue;
                totalVertices += mesh.vertexCount;
                long triangles = 0;
                for (var i = 0; i < mesh.subMeshCount; i++)
                    if (mesh.GetTopology(i) == MeshTopology.Triangles)
                        triangles += (long)mesh.GetIndexCount(i) / 3L;
                totalTriangles += triangles;

                if (triangles > settings.maxMeshTriangles)
                    Add(report, "Warning", "High triangle mesh", $"{triangles} triangles exceeds {settings.maxMeshTriangles}.", path);
                if (mesh.vertexCount > settings.maxMeshVertices)
                    Add(report, "Warning", "High vertex mesh", $"{mesh.vertexCount} vertices exceeds {settings.maxMeshVertices}.", path);
                if (mesh.subMeshCount > settings.maxSubMeshes)
                {
                    tooManySubMeshes++;
                    Add(report, "Warning", "High submesh count", $"{mesh.subMeshCount} submeshes can increase material slots and draw-call pressure.", path);
                }
                if (settings.requireUv0 && (mesh.uv == null || mesh.uv.Length == 0))
                {
                    missingUv++;
                    Add(report, "Warning", "Mesh has no UV0", "No primary UV coordinates were found.", path);
                }
                if (mesh.isReadable)
                {
                    readable++;
                    if (settings.warnReadableMeshes)
                        Add(report, "Warning", "Readable mesh retains CPU copy", "Read/Write is enabled; disable unless runtime CPU mesh access is required.", path);
                }
                if (settings.detectDegenerateTriangles && mesh.isReadable)
                    degenerate += CountDegenerate(mesh, path, report);
                if (mesh.bounds.size.sqrMagnitude <= 0f && mesh.vertexCount > 0)
                    Add(report, "Warning", "Mesh has invalid zero bounds", "Non-empty mesh has zero-sized bounds.", path);
            }

            Metric(report, "mesh_count", guids.Length, "count");
            Metric(report, "total_mesh_triangles", totalTriangles, "triangles");
            Metric(report, "total_mesh_vertices", totalVertices, "vertices");
            Metric(report, "mesh_missing_uv0_count", missingUv, "count");
            Metric(report, "mesh_high_submesh_count", tooManySubMeshes, "count");
            Metric(report, "readable_mesh_count", readable, "count");
            Metric(report, "degenerate_triangle_count", degenerate, "triangles");
        }

        public static void Analyze(CollectorReport report, int maxTriangles)
        {
            var settings = UnityCollectorSettings.LoadOrCreate();
            settings.maxMeshTriangles = maxTriangles;
            Analyze(report, settings);
        }

        private static int CountDegenerate(Mesh mesh, string path, CollectorReport report)
        {
            try
            {
                var vertices = mesh.vertices;
                var indices = mesh.triangles;
                var count = 0;
                for (var i = 0; i + 2 < indices.Length; i += 3)
                {
                    var a = vertices[indices[i]]; var b = vertices[indices[i + 1]]; var c = vertices[indices[i + 2]];
                    if (Vector3.Cross(b - a, c - a).sqrMagnitude <= 1e-12f) count++;
                }
                if (count > 0)
                    Add(report, "Warning", "Mesh contains degenerate triangles", $"Detected {count} zero-area triangle(s).", path);
                return count;
            }
            catch (System.Exception ex)
            {
                Add(report, "Warning", "Mesh topology could not be inspected", ex.Message, path);
                return 0;
            }
        }

        private static void Add(CollectorReport report, string severity, string title, string description, string source) =>
            report.issues.Add(new CollectorIssue { category = "asset", severity = severity, title = title, description = description, source = source });
        private static void Metric(CollectorReport report, string name, double value, string unit) =>
            report.metrics.Add(new CollectorMetric { name = name, value = value, unit = unit });
    }
}
