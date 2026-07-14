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
    if (isnan(input.UV.x) && isnan(input.UV.y) || pushData.materialIndex == UINT32_MAX) {
        float3 color = input.color;
        return float4(color, 1.0);
	}
	return textures[materials[pushData.materialIndex].baseColorTextureIndex].Sample(input.UV);
    
}

// float DistributionGGX(float NdotH, float roughness) {
//     float a = roughness * roughness;        // Remapping for more perceptual linearity
//     float a2 = a * a;
//     float NdotH2 = NdotH * NdotH;

//     float nom = a2;                         // Numerator: concentration factor
//     float denom = (NdotH2 * (a2 - 1.0) + 1.0);
//     denom = PI * denom * denom;             // Normalization factor

//     return nom / denom;                     // Normalized distribution
// }

// float GeometrySmith(float NdotV, float NdotL, float roughness) {
//     float r = roughness + 1.0;
//     float k = (r * r) / 8.0;               // Direct lighting remapping

//     // Geometry obstruction from view direction (masking)
//     float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
//     // Geometry obstruction from light direction (shadowing)
//     float ggx2 = NdotL / (NdotL * (1.0 - k) + k);

//     return ggx1 * ggx2;                     // Combined masking-shadowing
// }

// float3 FresnelSchlick(float cosTheta, float3 F0) {
//     return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
// }


// // Vertex shader entry point - Executes once per vertex
// [[shader("vertex")]]
// VSOutput vertMain(VSInput input, uint instanceID : SV_VulkanInstanceID) {
//     VSOutput output;

//     // Transform vertex position through the rendering pipeline
//     // Model -> World -> Camera -> Clip space transformation chain
//     float4 worldPosition = mul(modelTransform[instanceID].model, float4(input.position, 1.0));
//     output.position = mul(vpTransform.projection, mul(vpTransform.view, worldPosition));

//     // Pass world position for fragment lighting calculations
//     // Fragment shader needs world space position to calculate light vectors
//     output.worldPosition = worldPosition.xyz;

//     // Transform normal from model space to world space
//     // Use only rotation/scale part of model matrix (upper-left 3x3)
//     // Normalize to ensure unit length after transformation
//     output.normal = normalize(mul((float3x3)modelTransform[instanceID].model, input.normal));

//     // Pass through texture coordinates unchanged
//     // UV coordinates are typically in [0,1] range and don't need transformation
//     output.UV = input.UV;

//     // Pass tangent vector for normal mapping
//     // Will be used in fragment shader to construct tangent-space basis
//     output.tangent = input.tangent;

//     return output;
// }

// // Fragment shader entry point - Executes once per pixel
// [[shader("fragment")]]
// float4 fragMain(VSOutput input) : SV_TARGET
// {
// 	MaterialBuffer material = materials[pushData.materialIndex];

//     // === MATERIAL PROPERTY SAMPLING ===
//     // Sample base color texture and apply material color factor
//     float4 baseColor = textures[material.baseColorTextureIndex].Sample(input.UV) * material.baseColorFactor;

// 	// Sample metallic-roughness texture (metallic=B channel, roughness=G channel)
// 	// glTF standard: metallic stored in blue, roughness in green
// 	float2 metallicRoughness = {1.0, 1.0};
// 	if (material.metallicRoughnessTextureIndex != UINT32_MAX) {
// 		metallicRoughness = textures[material.metallicRoughnessTextureIndex].Sample(input.UV).bg;
// 	}
//     float metallic = metallicRoughness.x * material.metallicRoughnessFactor.x;
//     float roughness = metallicRoughness.y * material.metallicRoughnessFactor.y;

// 	// Sample ambient occlusion (typically stored in red channel)
// 	float ao = material.occlusionStrength;
// 	if (material.occlusionTextureIndex != UINT32_MAX) {
// 		ao *= textures[material.occlusionTextureIndex].Sample(input.UV).r;
// 	}

// 	// Sample emissive texture for self-illuminating materials
// 	float3 emissive = material.emissiveFactor;
// 	if (material.emissiveTextureIndex != UINT32_MAX) {
// 		emissive *= textures[material.emissiveTextureIndex].Sample(input.UV).rgb;
// 	}

//     // === NORMAL CALCULATION ===
//     // Start with interpolated surface normal
//     float3 N = normalize(input.normal);

//     // Apply normal mapping if texture is available
//     if (material.normalTextureIndex != UINT32_MAX) {
//         // Sample normal map and convert from [0,1] to [-1,1] range
//         float3 tangentNormal = textures[material.normalTextureIndex].Sample(input.UV).xyz * 2.0 - 1.0;

//         // Construct tangent-space to world-space transformation matrix (TBN)
//         float3 T = normalize(input.tangent.xyz);              // Tangent
//         float3 B = normalize(cross(N, T)) * input.tangent.w;  // Bitangent (w = handedness)
//         float3x3 TBN = float3x3(T, B, N);                     // Tangent-Bitangent-Normal matrix

//         // Transform normal from tangent space to world space
//         N = normalize(mul(tangentNormal, TBN));
//     }

//     // === LIGHTING SETUP ===
//     // Calculate view direction (fragment to camera)
//     float3 V = normalize(vpTransform.cameraPosition.xyz - input.worldPosition);

//     // Calculate reflection vector for environment mapping
//     float3 R = reflect(-V, N);

//     // === PBR MATERIAL SETUP ===
//     // Calculate F0 (reflectance at normal incidence)
//     // Non-metals: low reflectance (~0.04), Metals: colored reflectance from base color
//     float3 F0 = float3(0.04, 0.04, 0.04);  // Dielectric default
//     F0 = lerp(F0, baseColor.rgb, metallic); // Lerp to metallic behavior

//     // Initialize outgoing radiance accumulator
//     float3 Lo = float3(0.0, 0.0, 0.0);

//     // === DIRECT LIGHTING LOOP ===
//     // Calculate contribution from each light source
// 	for (int i = 0; i < vpTransform.lightCount; ++i) {
// 		float3 lightPos = { lights[i].position.x, lights[i].position.y, lights[i].transform._43};
//         float3 lightColor = lights[i].color.rgb;

//         // Calculate light direction and attenuation
//         float3 L = normalize(lightPos - input.worldPosition);      // Light direction
//         float distance = length(lightPos - input.worldPosition);   // Distance for falloff
//         float attenuation = 1.0 / (distance * distance);     // Inverse square falloff
//         float3 radiance = lightColor * attenuation;           // Attenuated light color

//         // Calculate half vector (between view and light directions)
//         float3 H = normalize(V + L);

//         // === BRDF EVALUATION ===
//         // Calculate all necessary dot products for BRDF terms
//         float NdotL = max(dot(N, L), 0.0);  // Lambertian falloff
//         float NdotV = max(dot(N, V), 0.0);  // View angle
//         float NdotH = max(dot(N, H), 0.0);  // Half vector for specular
//         float HdotV = max(dot(H, V), 0.0);  // For Fresnel calculation

//         // Evaluate Cook-Torrance BRDF components
//         float D = DistributionGGX(NdotH, roughness);    // Normal distribution
//         float G = GeometrySmith(NdotV, NdotL, roughness); // Geometry function
//         float3 F = FresnelSchlick(HdotV, F0);           // Fresnel reflectance

//         // Calculate specular BRDF
//         float3 numerator = D * G * F;
//         float denominator = 4.0 * NdotV * NdotL + 0.0001; // Prevent division by zero
//         float3 specular = numerator / denominator;

//         // === ENERGY CONSERVATION ===
//         // Fresnel term represents specular reflection ratio
//         float3 kS = F;                          // Specular contribution
//         float3 kD = float3(1.0, 1.0, 1.0) - kS; // Diffuse contribution (energy conservation)
//         kD *= 1.0 - metallic;                   // Metals have no diffuse reflection

//         // === RADIANCE ACCUMULATION ===
//         // Combine diffuse (Lambertian) and specular (Cook-Torrance) terms
//         // Multiply by incident radiance and cosine foreshortening
//         Lo += (kD * baseColor.rgb / PI + specular) * radiance * NdotL;
//     }

//     // === AMBIENT AND EMISSIVE ===
//     // Add simple ambient lighting (should be replaced with IBL in production)
//     float3 ambient = float3(0.03, 0.03, 0.03) * baseColor.rgb * ao;

//     // Combine all lighting contributions
//     float3 color = ambient + Lo + emissive;

//     // === HDR TONE MAPPING AND GAMMA CORRECTION ===
//     // Apply Reinhard tone mapping to compress HDR values to [0,1] range
//     color = color / (color + float3(1.0, 1.0, 1.0));

//     // Apply gamma correction for sRGB display (inverse gamma)
//     color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));

//     // Output final color with original alpha
//     return float4(color, baseColor.a);
// }
