// Thesis practical local-edit invalidation pass.
// Separate shader keeps the existing Novelty01 binding-mode ABI unchanged.
//
// t0: [count, blockIndex0, blockIndex1, ...]
// u0: Novelty01 BlockDescriptor buffer

#define N01_DIRTY_EDIT (1u << 4u)

struct BlockDescriptor
{
    uint firstGroupIndex;
    uint groupCount;
    uint dirtyFlags;
    uint validFlags;

    float3 boundsMin;
    float maximumMotionMm;

    float3 boundsMax;
    float minimumCollisionMarginMm;

    float maximumResidual;
    float maximumBoundaryErrorMm;
    uint descriptorVersion;
    uint lastUpdatedFrame;
};

StructuredBuffer<uint> ThesisEditBlockIndices : register(t0);
RWStructuredBuffer<BlockDescriptor> ThesisBlocks : register(u0);

[numthreads(64, 1, 1)]
void ThesisInvalidateBlocksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint count = ThesisEditBlockIndices[0];
    const uint localIndex = dispatchThreadId.x;
    if (localIndex >= count)
    {
        return;
    }

    const uint blockIndex = ThesisEditBlockIndices[localIndex + 1u];
    BlockDescriptor block = ThesisBlocks[blockIndex];
    block.dirtyFlags |= N01_DIRTY_EDIT;
    ThesisBlocks[blockIndex] = block;
}
