public module bindings;

struct VSInput {
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

struct VSOutput {
	float4 position : SV_Position;
	float3 worldPosition : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
	uint materialIndex;
	MeshType meshType;
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

enum MeshType { NORMAL, PROBE };
struct Metadata {
	uint materialIndex;
	MeshType type;
};

struct DDGIProbeBounds {
	int xLower;
	int xUpper;
	int yLower;
	int yUpper;
	int zLower;
	int zUpper;
};

[vk::binding(0, 0)]
StructuredBuffer<ModelTransformBuffer> modelTransform;
[vk::binding(1, 0)]
ConstantBuffer<DrawCallBuffer> vpTransform;
[vk::binding(2, 0)]
StructuredBuffer<MaterialBuffer> materials;
[vk::binding(3, 0)]
Sampler2D textures[];
[vk::binding(4, 0)]
StructuredBuffer<LightBuffer> lights;
[vk::binding(5, 0)]
RaytracingAccelerationStructure accelerationStructure;
[vk::binding(6, 0)]
StructuredBuffer<VSInput> vertices;
[vk::binding(7, 0)]
StructuredBuffer<uint32_t> indices;
[vk::binding(8, 0)]
StructuredBuffer<tlasLUTBuffer> tlasLut;
[vk::binding(9, 0)]
StructuredBuffer<Metadata> metadata;
[vk::binding(10, 0)]
StructuredBuffer<float3> ddgiProbePositions;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(11, 0)]
Sampler2D ddgiIrradianceSampler;
[vk::image_format("rg16f")]
[vk::binding(12, 0)]
Sampler2D ddgiDepthSampler;
[vk::binding(13, 0)]
StructuredBuffer<float3> ddgiProbeSamples;
[vk::binding(14, 0)]
ConstantBuffer<DDGIProbeBounds> ddgiBounds;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(15, 0)]
RWTexture2D<float3> ddgiRadianceTransmissionTexture;
[vk::image_format("r16f")]
[vk::binding(16, 0)]
RWTexture2D<float> ddgiDistanceTransmissionTexture;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(17, 0)]
RWTexture2D<float3> ddgiIrradianceTexture;
[vk::image_format("rg16f")]
[vk::binding(18, 0)]
RWTexture2D<float2> ddgiDepthTexture;
[vk::image_format("r32ui")]
[vk::binding(19, 0)] 
RWTexture2D<uint> ddgiDepthSampleCountTexture;

static const float EPSILON = 0.01;
static const float PI = 3.14159265359;
static const uint32_t UINT32_MAX = 4294967295;
static const int3 DDGI_PROBE_DIMENSIONS = { 32, 32, 4 };
static const int RAY_COUNT = 192;
static const float HYSTERESIS = 0.85;
