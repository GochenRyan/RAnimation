#include "NRI.hlsl"

struct Constants
{
    uint selectedPickID;
    uint resolutionX;
    uint resolutionY;
    float outlineR;
    float outlineG;
    float outlineB;
};

NRI_RESOURCE(Texture2D<uint>, g_IdTexture, t, 0, 0);
NRI_ROOT_CONSTANTS(Constants, g_PushConstants, 0, 1);

float4 main(float4 svPosition : SV_Position) : SV_Target
{
    int2 p = int2(svPosition.xy);
    uint sel = g_PushConstants.selectedPickID;

    int2 maxP = int2(int(g_PushConstants.resolutionX) - 1, int(g_PushConstants.resolutionY) - 1);

    uint center = g_IdTexture.Load(int3(p, 0));
    uint nl = g_IdTexture.Load(int3(int2(max(p.x - 1, 0), p.y), 0));
    uint nr = g_IdTexture.Load(int3(int2(min(p.x + 1, maxP.x), p.y), 0));
    uint nu = g_IdTexture.Load(int3(int2(p.x, max(p.y - 1, 0)), 0));
    uint nd = g_IdTexture.Load(int3(int2(p.x, min(p.y + 1, maxP.y)), 0));

    bool centerSel = (center == sel);
    bool anyNeighborSel = (nl == sel) || (nr == sel) || (nu == sel) || (nd == sel);

    // Outer ring: a pixel just outside the selected silhouette (center not selected, a neighbor is).
    bool edge = (!centerSel) && anyNeighborSel;
    if (!edge)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    return float4(g_PushConstants.outlineR, g_PushConstants.outlineG, g_PushConstants.outlineB, 1.0f);
}
