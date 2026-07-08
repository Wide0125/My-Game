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
    // float4 worldPos = mul(ubo.model, float4(input.inPosition, 1.0));
    // printf("world pos: %f, %f, %f\n", worldPos.x, worldPos.y, worldPos.z);
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    // printf("clip pos: %f, %f, %f, %f\n", output.pos.x, output.pos.y, output.pos.z, output.pos.w);
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

struct PushData { uint textureIndex; };

[[vk::binding(1, 0)]] Sampler2D textures[2];
[[vk::push_constant]]
ConstantBuffer<PushData> pushData;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    // printf("fragment shader executing...\n");
    if (isnan(vertIn.fragTexCoord.x) && isnan(vertIn.fragTexCoord.y)) {
        return {0, 0, 0, 0};
    }
    return textures[pushData.textureIndex].Sample(vertIn.fragTexCoord);
}
