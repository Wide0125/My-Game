public module bindings;

struct VSInput {
    float3 position : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

struct ModelTransformBuffer {
    float4x4 model;
	float3x3 normal;
};

struct DrawCallBuffer {
	float3 cameraPosition;
    float4x4 view;
	float4x4 projection;
	int lightCount;
}

struct LightBuffer {
	enum LightType { directional, spot, point };
	LightType type;
	float3 color;
	float intensity;
	float range;

	float3 position;
	float3 direction;
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

	enum AlphaMode { OPAQUE, MASK, BLEND };
	AlphaMode alphaMode;
	float alphaCutoff;
};

struct tlasLUTBuffer {
	uint materialIndex;
	uint indexStart;
	uint vertexOffset;
};

enum MeshType {NORMAL, PROBE};
struct Metadata { 
    uint materialIndex;
	MeshType type;
};

[vk::binding(0, 0)] StructuredBuffer<ModelTransformBuffer> modelTransform;
[vk::binding(1, 0)] ConstantBuffer<DrawCallBuffer> vpTransform;
[vk::binding(2, 0)] StructuredBuffer<MaterialBuffer> materials;
[vk::binding(3, 0)] Sampler2D textures[];
[vk::binding(4, 0)] StructuredBuffer<LightBuffer> lights;
[vk::binding(5, 0)] RaytracingAccelerationStructure accelerationStructure;
[vk::binding(6, 0)] StructuredBuffer<VSInput> vertices;
[vk::binding(7, 0)] StructuredBuffer<uint32_t> indices;
[vk::binding(8, 0)] StructuredBuffer<tlasLUTBuffer> tlasLut;
[vk::binding(9, 0)] StructuredBuffer<Metadata> metadata;
[vk::binding(10, 0)] StructuredBuffer<float3> ddgiProbePositions;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(11, 0)] RWTexture2D ddgiIrradianceTextures[];
[vk::image_format("rg16f")]
[vk::binding(12, 0)] RWTexture2D ddgiDepthTextures[];
