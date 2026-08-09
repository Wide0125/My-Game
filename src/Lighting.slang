public module lighting;

import bindings;

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

VSOutput intersectionVSOutput(
	uint instanceID, uint primIndex, float2 barycentrics, float3x4 o2w, float4x3 w2o
) {
	uint indexOffset = tlasLut[instanceID].indexStart;
	uint vertexOffset = tlasLut[instanceID].vertexOffset;

	uint i0 = indices[indexOffset + (primIndex * 3 + 0)];
	uint i1 = indices[indexOffset + (primIndex * 3 + 1)];
	uint i2 = indices[indexOffset + (primIndex * 3 + 2)];

	VSInput vertex0 = vertices[i0];
	VSInput vertex1 = vertices[i1];
	VSInput vertex2 = vertices[i2];

	float w0 = 1.0 - barycentrics.x - barycentrics.y;
	float w1 = barycentrics.x;
	float w2 = barycentrics.y;

	VSOutput output;

	output.worldPosition =
		mul(o2w,
			float4(vertex0.position * w0 + vertex1.position * w1 + vertex2.position * w2, 1.0));
	output.normal =
		mul(float4(vertex0.normal * w0 + vertex1.normal * w1 + vertex2.normal * w2, 0), w2o);
	output.color = vertex0.color * w0 + vertex1.color * w1 + vertex2.color * w2;
	output.UV = vertex0.UV * w0 + vertex1.UV * w1 + vertex2.UV * w2;
	output.tangent.xyz =
		mul(o2w,
			float4((vertex0.tangent * w0 + vertex1.tangent * w1 + vertex2.tangent * w2).xyz, 0));
	output.tangent.w = vertex0.tangent.w;
	output.materialIndex = tlasLut[instanceID].materialIndex;
	output.meshType = MeshType::NORMAL;

	return output;
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float num = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}
float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

float3 inShadow(
	float3 point,
	float3 lightRadiance,
	float3 lightPosition,
	float3 lightDirection,
	LightBuffer::LightType lightType
) {
	// return lightRadiance;

	float3 radiance = lightRadiance;
	RayDesc shadowRayDesc;
	shadowRayDesc.Origin = point;
	shadowRayDesc.Direction = lightDirection;
	shadowRayDesc.TMin = EPSILON;
	if (lightType == LightBuffer::LightType::directional) {
		shadowRayDesc.TMax = 1e4;
	} else {
		shadowRayDesc.TMax = distance(lightPosition, point);
	}

	RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH>
		shadowQuery;
	let rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;

	shadowQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, shadowRayDesc);

	while (shadowQuery.Proceed()) {
		uint instanceIndex = shadowQuery.CandidateRayInstanceCustomIndex();
		uint primIndex = shadowQuery.CandidatePrimitiveIndex();

		float2 uv =
			intersectionUV(instanceIndex, primIndex, shadowQuery.CandidateTriangleBarycentrics());

		MaterialBuffer material = materials[tlasLut[instanceIndex].materialIndex];
		float4 intersectionColor =
			textures[NonUniformResourceIndex(material.baseColorTextureIndex)].SampleLevel(uv, 0) *
			material.baseColorFactor;

		if (material.alphaMode == MaterialBuffer::AlphaMode::MASK) {
			if (intersectionColor.a > material.alphaCutoff) {
				shadowQuery.CommitNonOpaqueTriangleHit();
			}
		} else {
			if (intersectionColor.a == 1.0) {
				shadowQuery.CommitNonOpaqueTriangleHit();
			} else {
				float absorptionStrength = intersectionColor.a * 2.0;
				float3 absorptionCoefficient = (1.0 - intersectionColor.rgb) * absorptionStrength;
				radiance *= exp(-absorptionCoefficient);
			}
		}
	}

	if (shadowQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		return float3(0);
	} else {
		return radiance;
	}
};

float3 irradianceTexelFetch(float3 vector, int probeIndex) {
	int2 texelID = { (probeIndex % 64) * 8 + 1, (probeIndex / 64) * 8 + 1 };

	float3 octaVector = vector / (abs(vector.x) + abs(vector.y) + abs(vector.z));

	float2 UV;
	if (vector.z < 0) {
		UV = float2(
			(1.0 - abs(octaVector.x)) * sign(octaVector.x),
			(1.0 - abs(octaVector.y)) * sign(octaVector.y)
		);
	} else {
		UV = octaVector.xy;
	}
	UV *= 3;
	UV += 3;
	int2 texelUV = int2(floor(UV));
	return ddgiIrradianceSampler.SampleLevelZero((texelID + texelUV) / 512).rgb;
}

float2 depthTexelFetch(float3 vector, int probeIndex) {
	int2 texelID = { (probeIndex % 64) * 16 + 1, (probeIndex / 64) * 16 + 1 };

	float3 octaVector = vector / (abs(vector.x) + abs(vector.y) + abs(vector.z));

	float2 UV;
	if (vector.z < 0) {
		UV = float2(
			(1.0 - abs(octaVector.x)) * sign(octaVector.x),
			(1.0 - abs(octaVector.y)) * sign(octaVector.y)
		);
	} else {
		UV = octaVector.xy;
	}
	UV *= 7;
	UV += 7;
	int2 texelUV = int2(floor(UV));
	return ddgiDepthSampler.SampleLevelZero((texelID + texelUV) / 1024).rg;
}

float4 getPointRadiance(
	float3 worldPosition,
	float3 N,
	float2 UV,
	float4 baseColor,
	float metallic,
	float roughness,
	float ao,
	MaterialBuffer::AlphaMode alphaMode,
	float3 cameraPosition
) {
	float3 V = normalize(cameraPosition - worldPosition);

	float3 F0 = float3(0.04);
	F0 = lerp(F0, baseColor.rgb, metallic);

	float3 Lo = float3(0.0);

	float3 kD;
	for (int i = 0; i < vpTransform.lightCount; ++i) {
		LightBuffer currLight = lights[i];
		float3 L;
		if (currLight.type == LightBuffer::LightType::point) {
			L = normalize(currLight.position - worldPosition);
		} else {
			L = -normalize(currLight.direction);
		}
		float3 H = normalize(V + L);
		float3 radiance;
		if (currLight.type == LightBuffer::LightType::point) {
			float distance = length(currLight.position - worldPosition);
			float attenuation = 1.0 / (distance * distance);
			radiance = currLight.intensity * currLight.color * attenuation;
		} else {
			radiance = currLight.intensity * currLight.color;
		}
		radiance = inShadow(worldPosition, radiance, currLight.position, L, currLight.type);
		if (!(radiance.x == 0.0 && radiance.y == 0.0 && radiance.z == 0.0)) {

			float NDF = DistributionGGX(N, H, roughness);
			float G = GeometrySmith(N, V, L, roughness);
			float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

			float3 numerator = NDF * G * F;
			float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
			float3 specular = numerator / denominator;

			float3 kS = F;
			kD = float3(1.0) - kS;
			kD *= 1.0 - metallic;

			float NdotL = max(dot(N, L), 0.0);
			Lo += (kD * baseColor.rgb / PI + specular) * radiance * NdotL;
		}
	}

	if (worldPosition.x >= ddgiBounds.xLower && worldPosition.x <= ddgiBounds.xUpper &&
		worldPosition.y >= ddgiBounds.yLower && worldPosition.y <= ddgiBounds.yUpper &&
		worldPosition.z >= ddgiBounds.zLower && worldPosition.z <= ddgiBounds.zUpper) {
		int3 lowerBounds = int3(floor(worldPosition));

		for (int x = lowerBounds.x; x < lowerBounds.x + 1; ++x) {
			for (int y = lowerBounds.y; y < lowerBounds.y + 1; ++y) {
				for (int z = lowerBounds.z; z < lowerBounds.z + 1; ++z) {
					int probeIndex = probeCoordinatesToIndex(x, y, z);

					float3 dir = ddgiProbePositions[probeIndex] - worldPosition;
					float r = length(dir);
					dir *= 1 / r;

					float weight = (dot(dir, N) + 1) * 0.5;

					float3 distances = abs(ddgiProbePositions[probeIndex] - worldPosition);
					weight *= (1 - (x - lowerBounds.x) - distances.x) *
							  (1 - (y - lowerBounds.y) - distances.y) *
							  (1 - (z - lowerBounds.z) - distances.z);

					float2 temp = depthTexelFetch(-dir, probeIndex);
					float mean = temp.r;
					float mean2 = temp.g;
					if (r > mean) {
						float variance = abs(pow(mean, 2) - mean2);
						weight *= variance / (variance + pow(r - mean, 2));
					}
					Lo += kD * (baseColor.rgb / PI) * irradianceTexelFetch(N, probeIndex);
				}
			}
		}
	}

	float3 ambient = float3(0) * baseColor.rgb * ao;
	float3 color = ambient + Lo;

	if (alphaMode == MaterialBuffer::AlphaMode::BLEND) {
		return float4(color, baseColor.a);
	} else {
		return float4(color, 1.0);
	}
}
