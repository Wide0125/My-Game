struct VSInput {
    float3 position : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

struct VSOutput
{
	float4 position : SV_Position;
	float3 worldPosition : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

struct ModelTransformBuffer {
    float4x4 model;
	float3x3 normal;
};

struct VPTransformBuffer {
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

struct PushData { 
    uint materialIndex;
};

[[vk::push_constant]] ConstantBuffer<PushData> pushData;

[[vk::binding(0, 0)]] StructuredBuffer<ModelTransformBuffer> modelTransform;
[[vk::binding(1, 0)]] ConstantBuffer<VPTransformBuffer> vpTransform;
[[vk::binding(2, 0)]] StructuredBuffer<MaterialBuffer> materials;
[[vk::binding(3, 0)]] Sampler2D textures[];
[[vk::binding(4, 0)]] StructuredBuffer<LightBuffer> lights;
[[vk::binding(5, 0)]] RaytracingAccelerationStructure accelerationStructure;

static const float PI = 3.14159265359;
static const uint32_t UINT32_MAX = 4294967295;

[shader("vertex")]
VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID) {
    VSOutput output;
	float4 worldPosition = mul(modelTransform[instanceID].model, float4(input.position, 1.0));
	output.position = mul(vpTransform.projection, mul(vpTransform.view, worldPosition));
	output.worldPosition = worldPosition.xyz;
	output.color = input.color;
    output.UV = input.UV;
	output.normal = normalize(mul(modelTransform[instanceID].normal, input.normal));
	output.tangent.xyz = normalize(mul(modelTransform[instanceID].normal, input.tangent.xyz));
	output.tangent.w = input.tangent.w;
    return output;
}


float3 fresnelSchlick(float cosTheta, float3 F0) {
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

static const float EPSILON = 0.01;
bool inShadow(float3 point, float3 lightDirection) {
	// return false;

	RayDesc shadowRayDesc;
	shadowRayDesc.Origin = point;
	shadowRayDesc.Direction = lightDirection;
	shadowRayDesc.TMin = EPSILON;
	shadowRayDesc.TMax = 1e4;

	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadowQuery;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
	
	shadowQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, shadowRayDesc);
	shadowQuery.Proceed();

	return shadowQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
};

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
	MaterialBuffer currMaterial = materials[pushData.materialIndex];

	float3 N = normalize(input.normal);
	if (currMaterial.normalTextureIndex != UINT32_MAX) {
		float3 T = normalize(input.tangent.xyz);
		T = normalize(T - dot(T, N) * N);
		float3 B = cross(N, T) * input.tangent.w;
		float3x3 TBN = float3x3(T, B, N);
		float3 mappedNormal = textures[currMaterial.normalTextureIndex].Sample(input.UV).xyz * 2.0 - 1.0;
		N = normalize(mul(mappedNormal, TBN));
	}

	float3 V = normalize(vpTransform.cameraPosition - input.worldPosition);

	float4 baseColor = currMaterial.baseColorFactor;
	if (currMaterial.baseColorTextureIndex != UINT32_MAX) {
		baseColor *= textures[currMaterial.baseColorTextureIndex].Sample(input.UV);
	}
	if (currMaterial.alphaMode == MaterialBuffer::AlphaMode::MASK && baseColor.a < currMaterial.alphaCutoff) {discard;}

	float metallic = currMaterial.metallicRoughnessFactor.r;
	float roughness = currMaterial.metallicRoughnessFactor.g;
	if (currMaterial.metallicRoughnessTextureIndex != UINT32_MAX) {
		metallic *= textures[currMaterial.metallicRoughnessTextureIndex].Sample(input.UV).r;
		roughness *= textures[currMaterial.metallicRoughnessTextureIndex].Sample(input.UV).g;
	}
	float ao = currMaterial.occlusionStrength;
	if (currMaterial.occlusionTextureIndex != UINT32_MAX) {
		ao *= textures[currMaterial.occlusionTextureIndex].Sample(input.UV).r;
	}

	float3 F0 = float3(0.04);
	F0 = lerp(F0, baseColor.rgb, metallic);

	float3 Lo = float3(0.0);

	for (int i = 0; i < vpTransform.lightCount; ++i) {
		LightBuffer currLight = lights[i];
		float3 L;
		if (currLight.type == LightBuffer::LightType::point) {
			L = normalize(currLight.position - input.worldPosition);
		}
		else {
			L = normalize(currLight.direction);
		}
		float3 H = normalize(V + L);
		if (!inShadow(input.worldPosition, L)) {
			float3 radiance;
			if (currLight.type == LightBuffer::LightType::point) {
				float distance = length(currLight.position - input.worldPosition);
				float attenuation = 1.0 / (distance * distance);
				radiance = currLight.intensity * currLight.color * attenuation;
			}
			else {
				radiance = currLight.intensity * currLight.color;
			}

			float NDF = DistributionGGX(N, H, roughness);
			float G = GeometrySmith(N, V, L, roughness);
			float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
	
			float3 numerator = NDF * G * F;	
			float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
			float3 specular = numerator / denominator;

			float3 kS = F;
			float3 kD = float3(1.0) - kS;
			kD *= 1.0 - metallic;

			float NdotL = max(dot(N, L), 0.0);
			Lo += (kD * baseColor.rgb / PI + specular) * radiance * NdotL;
		}
    }
	float3 ambient = float3(0.03) * baseColor.rgb * ao;
	float3 color = ambient + Lo;

	// color = color / (color + float3(1.0));
    // color = pow(color, float3(1.0/2.2));

	if (currMaterial.alphaMode == MaterialBuffer::AlphaMode::BLEND) {
		return float4(color, baseColor.a);
	}
	else {
		return float4(color, 1.0);
	}
}	
