using UnityEditor;
using UnityEngine;

namespace Dip.UnityCollector.Editor.Configuration
{
    public sealed class UnityCollectorSettings : ScriptableObject
    {
        [Header("Asset QA")]
        public int maxTextureSize = 4096;
        public int maxMeshTriangles = 100000;
        public int maxMeshVertices = 150000;
        public int maxSubMeshes = 8;
        public int maxMaterialsPerRenderer = 8;
        public int maxShaderKeywords = 64;
        public int requireMipmapsAboveSize = 512;
        public int warnUncompressedTextureAboveSize = 1024;
        public bool warnReadableTextures = true;
        public bool warnReadableMeshes = false;
        public bool requireUv0 = true;
        public bool detectDegenerateTriangles = true;
        public bool requireLodForLargeMeshes = true;

        [Header("Renderer / scene pressure")]
        public int maxRenderers = 10000;
        public int maxSkinnedMeshRenderers = 2000;
        public int maxTransparentRenderers = 2000;
        public int maxShadowCastingRenderers = 5000;
        public int maxParticleSystems = 2000;

        [Header("Scene / Animation QA")]
        public int maxSceneGameObjects = 50000;
        public int maxAnimationCurvesPerClip = 2000;
        public bool analyzeAllEnabledBuildScenes = true;
        public int maxDependencyFindings = 100;
        public int maxScriptHeuristicWarnings = 50;

        [Header("Runtime performance QA")]
        public double warningFrameTimeP95Ms = 16.6667;
        public double criticalFrameTimeP95Ms = 25.0;
        public double warningMonoMemoryMb = 2048.0;
        public string runtimePerformanceReportPath = "Library/DIP/dip-runtime-performance.json";

        public static UnityCollectorSettings LoadOrCreate()
        {
            const string path = "Assets/DipUnityCollectorSettings.asset";
            var settings = AssetDatabase.LoadAssetAtPath<UnityCollectorSettings>(path);
            if (settings != null) return settings;

            settings = CreateInstance<UnityCollectorSettings>();
            AssetDatabase.CreateAsset(settings, path);
            AssetDatabase.SaveAssets();
            return settings;
        }
    }
}
