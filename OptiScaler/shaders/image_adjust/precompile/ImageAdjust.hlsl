// ILION graphics enhancements: colour and local contrast of the upscaled picture, one pass, written for ILION.
// Never run with neutral values: the pass is left out of the chain then, so the picture stays bit exact.
// SDR (IsHdr = 0): values 0..1, contrast as an S curve that keeps black and white.
// HDR (IsHdr = 1): linear values above 1, contrast in log space around mid grey, nothing clamped.

#ifdef VK_MODE
cbuffer Params : register(b0, space0)
#else
cbuffer ImageAdjustCB : register(b0)
#endif
{
    int Width;
    int Height;
    int IsHdr;
    int SplitX; // > 0: pixels left of it show the original picture

    float Exposure; // brightness, a multiplier
    float GainR;    // temperature, luminance kept
    float GainG;
    float GainB;

    float ContrastPower; // 1 = unchanged
    float Pivot;
    float Saturation; // 1 = unchanged
    float Vibrance;   // 0 = unchanged, weighted by how dull a colour is

    float LocalContrast; // 0 = unchanged
    float LocalRadius;   // pixels
    float Pad0;
    float Pad1;
};

#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> InTexture : register(t0);

#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
RWTexture2D<float4> OutTexture : register(u0);

static const float3 LumaW = float3(0.2126f, 0.7152f, 0.0722f);

float Luma(float3 c) { return max(dot(c, LumaW), 0.0f); }

float LoadLuma(int2 p)
{
    p = clamp(p, int2(0, 0), int2(Width - 1, Height - 1));
    return Luma(InTexture.Load(int3(p, 0)).rgb);
}

// Average luminance of two rings of 8 samples around p: what counts as the neighbourhood for local contrast
float Neighbourhood(int2 p)
{
    const float2 dirs[8] = { float2(1, 0), float2(0.7071f, 0.7071f), float2(0, 1), float2(-0.7071f, 0.7071f),
                             float2(-1, 0), float2(-0.7071f, -0.7071f), float2(0, -1), float2(0.7071f, -0.7071f) };
    float sum = 0.0f;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        sum += LoadLuma(p + int2(dirs[i] * LocalRadius));
        sum += LoadLuma(p + int2(dirs[(i + 1) % 8] * LocalRadius * 0.5f));
    }

    return sum / 16.0f;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint) Width || id.y >= (uint) Height)
        return;

    const int2 p = int2(id.xy);
    const float4 src = InTexture.Load(int3(p, 0));

    if (SplitX > 0)
    {
        if (p.x < SplitX - 1)
        {
            OutTexture[p] = src;
            return;
        }

        if (p.x <= SplitX) // a two pixel divider, mid grey
        {
            OutTexture[p] = float4(IsHdr != 0 ? 0.18f.xxx : 0.5f.xxx, src.a);
            return;
        }
    }

    float3 c = src.rgb * Exposure * float3(GainR, GainG, GainB);
    float l = Luma(c);

    if (ContrastPower != 1.0f && l > 1e-6f)
    {
        float nl;

        if (IsHdr != 0)
            // HDR: highlights can be far above 1, the change is limited to half a stop either way
            nl = l * exp2(clamp(log2(max(l / Pivot, 1e-6f)) * (ContrastPower - 1.0f), -0.5f, 0.5f));
        else if (l < 0.5f)
            nl = 0.5f * pow(saturate(2.0f * l), ContrastPower);
        else
            nl = 1.0f - 0.5f * pow(saturate(2.0f * (1.0f - l)), ContrastPower);

        c *= nl / l;
        l = nl;
    }

    if (LocalContrast > 0.0f && l > 1e-6f)
    {
        // Detail = how far the pixel is from its neighbourhood, in stops, limited so edges don't ring
        const float around = Neighbourhood(p) * Exposure;
        const float detail = clamp(log2((Luma(src.rgb) * Exposure + 1e-4f) / (around + 1e-4f)), -1.0f, 1.0f);
        const float scale = exp2(LocalContrast * 0.5f * detail);

        c *= scale;
        l *= scale;
    }

    if (Saturation != 1.0f || Vibrance != 0.0f)
    {
        const float mx = max(c.r, max(c.g, c.b));
        const float mn = min(c.r, min(c.g, c.b));
        const float sat = mx > 1e-6f ? saturate((mx - mn) / mx) : 0.0f;
        const float amount = max(Saturation + Vibrance * (1.0f - sat), 0.0f);

        c = l + (c - l) * amount;
    }

    if (IsHdr == 0)
        c = saturate(c);

    OutTexture[p] = float4(c, src.a);
}
