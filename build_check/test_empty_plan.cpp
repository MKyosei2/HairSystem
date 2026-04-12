#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "hairgpu/AssetIO.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/ActiveSet.h"
using namespace hairgpu;

static bool LoadHairAssetAny(const std::filesystem::path& path, HairAsset& asset, std::string& error)
{
    const std::string ext = path.extension().string();
    bool loaded = false;
    if (ext == ".csv") loaded = LoadHairAssetFromCurvesCsv(path, asset, error);
    else if (ext == ".hairbin") loaded = LoadHairAssetFromHairBinary(path, asset, error);
    else return false;
    if (!loaded) return false;
    ParticleChainCanonicalizationSettings s = {};
    s.targetPointCount = asset.particlesPerStrand;
    s.snapRootsToScalp = true;
    return CanonicalizeHairAssetToParticleChains(asset, s, error);
}
int main(){
    HairAsset asset; std::string error;
    if(!LoadHairAssetAny("/mnt/data/apps_work/docs/sample_assets/import_curves_template.csv", asset, error)){std::cerr<<error<<"\n"; return 1;}
    auto clusters = BuildActiveSetClusters(asset, {});
    ActiveSetPlan plan = {};
    plan.clusters = clusters;
    plan.scores.resize(clusters.size());
    plan.strandCorrectiveMask.assign(asset.strands.size(), 0u);
    plan.recommendedSimulationTiers.assign(asset.strands.size(), SimTier::Cached);
    for(size_t i=0;i<clusters.size();++i) plan.scores[i].clusterIndex = (uint32_t)i;
    plan.summary.clusterCount = (uint32_t)clusters.size();
    plan.summary.estimatedFixedCostMilliseconds = 6.0f;
    plan.summary.estimatedFrameMilliseconds = 6.0f;
    plan.summary.cachedTierCount = (uint32_t)asset.strands.size();
    if(!WriteActiveSetPlanJson("/mnt/data/apps_work/build_check/test_plan.json", asset, plan, error)){std::cerr<<error<<"\n"; return 2;}
    std::cout<<"ok clusters="<<clusters.size()<<"\n";
}
