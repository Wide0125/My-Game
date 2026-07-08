struct VSInput {
    float3 inPosition;
    float3 inNormal;
    float3 inColor;
    float2 inTexCoord;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]]
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput
{
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

struct PushData { uint textureIndex; };

[[vk::binding(1, 0)]] Sampler2D textures[];
[[vk::push_constant]]
ConstantBuffer<PushData> pushData;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    // if (isnan(vertIn.fragTexCoord.x) && isnan(vertIn.fragTexCoord.y)) {
    //     return {0, 0, 0, 0};
    // }
    return textures[pushData.textureIndex].Sample(vertIn.fragTexCoord);
}
