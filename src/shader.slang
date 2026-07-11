struct VSInput {
    float3 inPosition;
    float3 inNormal;
    float3 inColor;
    float2 inTexCoord;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 fragColor;
    float2 fragTexCoord;
};

struct ModelTransformBuffer {
    float4x4 model;
};

struct VPTransformBuffer {
    float4x4 view;
    float4x4 projection;
}

struct LightBuffer {
	enum LightType { directional, point, spot };
	LightType type;
	float3 color;
	float intensity;
	float range;
};

struct PushData { 
    uint textureIndex;
};

[[vk::push_constant]] ConstantBuffer<PushData> pushData;

[[vk::binding(0, 0)]] StructuredBuffer<ModelTransformBuffer> modelTransform;
[[vk::binding(1, 0)]] ConstantBuffer<VPTransformBuffer> vpTransform;
[shader("vertex")]
VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID) {
    VSOutput output;
    output.pos = mul(vpTransform.projection, mul(vpTransform.view, mul(modelTransform[instanceID].model, float4(input.inPosition, 1.0))));
    output.fragColor = input.inColor;
    output.fragTexCoord = input.inTexCoord;
    return output;
}

[[vk::binding(2, 0)]] Sampler2D textures[];
[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {

    if (isnan(vertIn.fragTexCoord.x) && isnan(vertIn.fragTexCoord.y) || pushData.textureIndex == -1) {
            float3 color = vertIn.fragColor;
            return float4(color, 1.0);
    }
    return textures[pushData.textureIndex].Sample(vertIn.fragTexCoord);
}
