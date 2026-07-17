struct VSInput {
    float3 position : POSITION;
	float3 normal : NORMAL;
	float3 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

struct VSOutput
{
	float4 position : SV_Position;
	float3 worldPosition : POSITION;
	float3 normal : NORMAL;
	float3 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

struct ModelTransformBuffer {
    float4x4 model;
};

struct VPTransformBuffer {
	float3 cameraPosition;
    float4x4 view;
	float4x4 projection;
	int lightCount;
}

struct LightBuffer {
	enum LightType { directional, point, spot };
	LightType type;
	float3 color;
	float intensity;
	float range;

	float3 position;
};

struct MaterialBuffer {
    uint baseColorTextureIndex;
	float4 baseColorFactor;

	uint metallicRoughnessTextureIndex;
	float2 metallicRoughnessFactor;

	uint normalTextureIndex;
	float normalScale;

	uint occlusionTextureIndex;
	float occlusionStrength;

	uint emissiveTextureIndex;
	float3 emissiveFactor;
};

struct PushData { 
    uint materialIndex;
};

[[vk::push_constant]] ConstantBuffer<PushData> pushData;

[[vk::binding(0, 0)]] StructuredBuffer<ModelTransformBuffer> modelTransform;
[[vk::binding(1, 0)]] ConstantBuffer<VPTransformBuffer> vpTransform;
[[vk::binding(2, 0)]] StructuredBuffer<MaterialBuffer> materials;
[[vk::binding(3, 0)]] Sampler2D textures[];
[[vk::binding(4, 0)]] ConstantBuffer<LightBuffer> lights[];

static const float PI = 3.14159265359;
static const uint32_t UINT32_MAX = 4294967295;

[shader("vertex")]
VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID) {
    VSOutput output;
	output.position = mul(vpTransform.projection, mul(vpTransform.view, mul(modelTransform[instanceID].model, float4(input.position, 1.0))));
	output.color = input.color;
    output.UV = input.UV;
    return output;
}



[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    // if (isnan(input.UV.x) && isnan(input.UV.y) || pushData.materialIndex == UINT32_MAX) {
    //     float3 color = input.color;
    //     return float4(color, 1.0);
	// }
	// return textures[materials[pushData.materialIndex].baseColorTextureIndex].Sample(input.UV);

	float3 N = normalize(input.normal);
	float3 V = normalize(vpTransform.cameraPosition - input.worldPosition);

	float3 outgoingLight = { 0.0, 0.0, 0.0 };

	for (int i = 0; i < lights.getCount; ) {

    }
    
}
