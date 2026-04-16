
#include "hairgpu/Types.h"
#include <iostream>
using namespace hairgpu;
int main(){
    auto asset = BuildSampleHybridHairAsset(896, 0, 129, 0.30f/128.0f);
    float miny=1e9f,maxy=-1e9f,minz=1e9f,maxz=-1e9f;
    for(const auto &p : asset.restPoints){
        if(p.y<miny) miny=p.y;
        if(p.y>maxy) maxy=p.y;
        if(p.z<minz) minz=p.z;
        if(p.z>maxz) maxz=p.z;
    }
    std::cout << asset.strands.size() << " " << asset.restPoints.size() << "\n";
    std::cout << miny << " " << maxy << " " << minz << " " << maxz << "\n";
}
