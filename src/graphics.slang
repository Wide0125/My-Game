import GraphicsBindings;
import ComputeBindings;
import Lighting;
import Constants;

[shader("vertex")]
VSOutput
vertMain(VSInput input, uint instanceID: SV_VulkanInstanceID, uint drawIndex: SV_DrawIndex) {
	VSOutput output;
	float4 worldPosition;
	if (metadata[drawIndex].type == MeshType::PROBE) {
		output.probeIndex = modelTransform[instanceID].probeIndex;
		output.probeOrigin =
			ddgiProbes[output.probeIndex].position + ddgiProbes[output.probeIndex].offset;
		float4x4 probeTransform = float4x4(
			float4(0.1, 0, 0, output.probeOrigin.x),
			float4(0, 0.1, 0, output.probeOrigin.y),
			float4(0, 0, 0.1, output.probeOrigin.z),
			float4(0, 0, 0, 1)
		);
		worldPosition = mul(probeTransform, float4(input.position, 1.0));
	} else {
		worldPosition = mul(modelTransform[instanceID].model, float4(input.position, 1.0));
	}
	output.position = mul(vpTransform.projection, mul(vpTransform.view, worldPosition));
	output.worldPosition = worldPosition.xyz;
	output.color = input.color;
	output.UV = input.UV;
	output.normal = normalize(mul(modelTransform[instanceID].normal, input.normal));
	output.tangent.xyz =
		normalize(mul(modelTransform[instanceID].model, float4(input.tangent.xyz, 1))).xyz;
	output.tangent.w = input.tangent.w;
	output.materialIndex = metadata[drawIndex].materialIndex;
	output.meshType = metadata[drawIndex].type;
	return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
	if (input.meshType == MeshType::PROBE) {
		float3 vector = input.worldPosition - input.probeOrigin;
		return float4(
			irradianceTexelFetch(
				vector, input.probeIndex / PROBES_PER_CASCADE, input.probeIndex % PROBES_PER_CASCADE
			) + float3(0.01),
			1
		);
	}

	MaterialBuffer currMaterial = materials[input.materialIndex];

	float3 N = normalize(input.normal);
	if (currMaterial.normalTextureIndex != UINT32_MAX) {
		float3 T = normalize(input.tangent.xyz);
		T = normalize(T - dot(T, N) * N);
		float3 B = cross(N, T) * input.tangent.w;
		float3x3 TBN = float3x3(T, B, N);
		float3 mappedNormal =
			textures[currMaterial.normalTextureIndex].Sample(input.UV).xyz * 2.0 - 1.0;
		N = normalize(mul(mappedNormal, TBN));
	}
	if (any(isnan(N))) {
		N = { 0, 0, 1 };
	}

	float4 baseColor = currMaterial.baseColorFactor;
	if (currMaterial.baseColorTextureIndex != UINT32_MAX) {
		baseColor *= textures[currMaterial.baseColorTextureIndex].Sample(input.UV);
	}
	if (currMaterial.alphaMode == MaterialBuffer::AlphaMode::MASK &&
		baseColor.a < currMaterial.alphaCutoff) {
		discard;
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
	return getPointRadiance(
		input.worldPosition,
		N,
		input.UV,
		baseColor,
		metallic,
		roughness,
		ao,
		currMaterial.alphaMode,
		vpTransform.cameraPosition,
		true
	);
}
