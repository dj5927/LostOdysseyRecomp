#pragma once

namespace gpu::bloom_prefilter
{
// Restore the guest pixel footprint before the original nine-tap bloom shader.
// Exact overlap weights also cover non-integral internal resolution ratios.
inline constexpr const char* PixelShader = R"hlsl(
Texture2D<float4> src : register(t0, space1);
float4 main(float4 pos : SV_Position) : SV_Target
{
    uint width, height;
    src.GetDimensions(width, height);
    float2 scale = float2(width, height) / float2(1280.0, 720.0);
    float2 lower = floor(pos.xy) * scale;
    float2 upper = lower + scale;
    int2 first = int2(floor(lower));
    int2 last = int2(ceil(upper));
    float4 sum = 0.0;
    float weightSum = 0.0;
    [loop] for (int y = first.y; y < last.y; ++y)
    {
        float wy = max(0.0, min(upper.y, float(y + 1)) - max(lower.y, float(y)));
        [loop] for (int x = first.x; x < last.x; ++x)
        {
            float wx = max(0.0, min(upper.x, float(x + 1)) - max(lower.x, float(x)));
            float weight = wx * wy;
            sum += src.Load(int3(clamp(int2(x, y), int2(0, 0), int2(width, height) - 1), 0)) * weight;
            weightSum += weight;
        }
    }
    return sum / max(weightSum, 1e-8);
}
)hlsl";
}
