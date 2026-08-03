struct VSInput {
    float3 position : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
};

enum MeshType {NORMAL, PROBE};
struct VSOutput
{
	float4 position : SV_Position;
	float3 worldPosition : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 UV : TEXCOORD0;
	float4 tangent : TANGENT;
	uint materialIndex;
	uint instanceID;
	MeshType meshType;
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
} // TODO: better name

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

struct Metadata { 
    uint materialIndex;
	MeshType type;
};

[[vk::binding(0, 0)]] StructuredBuffer<ModelTransformBuffer> modelTransform;
[[vk::binding(1, 0)]] ConstantBuffer<VPTransformBuffer> vpTransform;
[[vk::binding(2, 0)]] StructuredBuffer<MaterialBuffer> materials;
[[vk::binding(3, 0)]] Sampler2D textures[];
[[vk::binding(4, 0)]] StructuredBuffer<LightBuffer> lights;
[[vk::binding(5, 0)]] RaytracingAccelerationStructure accelerationStructure;
[[vk::binding(6, 0)]] StructuredBuffer<VSInput> vertices;
[[vk::binding(7, 0)]] StructuredBuffer<uint32_t> indices;
[[vk::binding(8, 0)]] StructuredBuffer<tlasLUTBuffer> tlasLut;
[[vk::binding(9, 0)]] StructuredBuffer<Metadata> metadata;


static const float PI = 3.14159265359;
static const uint32_t UINT32_MAX = 4294967295;

[shader("vertex")]
VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID, uint drawIndex : SV_DrawIndex) {
    VSOutput output;
	float4 worldPosition = mul(modelTransform[instanceID].model, float4(input.position, 1.0));
	output.position = mul(vpTransform.projection, mul(vpTransform.view, worldPosition));
	output.worldPosition = worldPosition.xyz;
	output.color = input.color;
    output.UV = input.UV;
	output.normal = normalize(mul(modelTransform[instanceID].normal, input.normal));
	output.tangent.xyz = normalize(mul(modelTransform[instanceID].normal, input.tangent.xyz));
	output.tangent.w = input.tangent.w;
	output.materialIndex = metadata[drawIndex].materialIndex;
	output.instanceID = instanceID;
	output.meshType = metadata[drawIndex].type;
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

float2 intersectionUV(uint instanceID, uint primIndex, float2 barycentrics) {
    uint indexOffset = tlasLut[instanceID].indexStart;
	uint vertexOffset = tlasLut[instanceID].vertexOffset;

    uint i0 = indices[indexOffset + (primIndex * 3 + 0)];
    uint i1 = indices[indexOffset + (primIndex * 3 + 1)];
    uint i2 = indices[indexOffset + (primIndex * 3 + 2)];

    float2 uv0 = vertices[i0 + vertexOffset].UV;
    float2 uv1 = vertices[i1 + vertexOffset].UV;
    float2 uv2 = vertices[i2 + vertexOffset].UV;

    float w0 = 1.0 - barycentrics.x - barycentrics.y;
    float w1 = barycentrics.x;
    float w2 = barycentrics.y;

    return w0 * uv0 + w1 * uv1 + w2 * uv2;
}

float3 inShadow(float3 point, float3 lightRadiance, float3 lightPosition, float3 lightDirection, LightBuffer::LightType lightType) {
	// return lightRadiance;

	float3 radiance = lightRadiance;
	RayDesc shadowRayDesc;
	shadowRayDesc.Origin = point;
	shadowRayDesc.Direction = lightDirection;
	shadowRayDesc.TMin = EPSILON;
	if (lightType == LightBuffer::LightType::directional) {
		shadowRayDesc.TMax = 1e4;
	}
	else {
		shadowRayDesc.TMax = distance(lightPosition, point);
	}

	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadowQuery;
    let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
	
	shadowQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, shadowRayDesc);

	while (shadowQuery.Proceed()) {
        uint instanceIndex = shadowQuery.CandidateRayInstanceCustomIndex();
        uint primIndex = shadowQuery.CandidatePrimitiveIndex();

        float2 uv = intersectionUV(instanceIndex, primIndex, shadowQuery.CandidateTriangleBarycentrics());

        MaterialBuffer material = materials[tlasLut[instanceIndex].materialIndex];
        float4 intersectionColor = textures[NonUniformResourceIndex(material.baseColorTextureIndex)].SampleLevel(uv, 0) * material.baseColorFactor;

		if (material.alphaMode == MaterialBuffer::AlphaMode::MASK) {
			if (intersectionColor.a > material.alphaCutoff) {
				shadowQuery.CommitNonOpaqueTriangleHit();
			}
		}
		else {
			if (intersectionColor.a == 1.0) {
				shadowQuery.CommitNonOpaqueTriangleHit();
			}
			else {
				float absorptionStrength = intersectionColor.a * 2.0;
				float3 absorptionCoefficient = (1.0 - intersectionColor.rgb) * absorptionStrength;
				radiance *= exp(-absorptionCoefficient);
			}
		}
    }

	if (shadowQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		return float3(0);
	}
	else {
		return radiance;
	}
};

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
	MaterialBuffer currMaterial = materials[input.materialIndex];

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
	if (input.meshType == MeshType::PROBE) {
		return float4(baseColor.rgb * (input.instanceID % 256) / 255, 1);
	}

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
			L = -normalize(currLight.direction);
		}
		float3 H = normalize(V + L);
		float3 radiance;
		if (currLight.type == LightBuffer::LightType::point) {
			float distance = length(currLight.position - input.worldPosition);
			float attenuation = 1.0 / (distance * distance);
			radiance = currLight.intensity * currLight.color * attenuation;
		}
		else {
			radiance = currLight.intensity * currLight.color;
		}
		radiance = inShadow(input.worldPosition, radiance, currLight.position, L, currLight.type);
		if (!(radiance.x == 0.0 && radiance.y == 0.0 && radiance.z == 0.0)) {

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

