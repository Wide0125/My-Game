import bindings;
import lighting;

[shader("compute")]
[numthreads(RAY_COUNT, 1, 1)]
void computeMain(uint3 probeIndex: SV_GroupID, uint3 rayIndex: SV_GroupThreadID) {
	RayDesc probeRayDesc;
	probeRayDesc.Origin = ddgiProbePositions
	[probeIndex.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
	 probeIndex.y * DDGI_PROBE_DIMENSIONS.z + probeIndex.z];
	probeRayDesc.Direction = ddgiProbeSamples[rayIndex.x];
	probeRayDesc.TMin = EPSILON;
	probeRayDesc.TMax = 1e4;

	RayQuery probeQuery;
	uint rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

	probeQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, probeRayDesc);

	float3 candidateTransmittanceFactor = float3(1);
	while (probeQuery.Proceed()) {
		uint instanceIndex = probeQuery.CandidateRayInstanceCustomIndex();
		uint primIndex = probeQuery.CandidatePrimitiveIndex();

		float2 uv =
			intersectionUV(instanceIndex, primIndex, probeQuery.CandidateTriangleBarycentrics());

		MaterialBuffer material = materials[tlasLut[instanceIndex].materialIndex];
		float4 intersectionColor =
			textures[NonUniformResourceIndex(material.baseColorTextureIndex)].SampleLevel(uv, 0) *
			material.baseColorFactor;
		if (material.alphaMode == MaterialBuffer::AlphaMode::MASK) {
			if (intersectionColor.a > material.alphaCutoff) {
				probeQuery.CommitNonOpaqueTriangleHit();
			}
		} else {
			if (intersectionColor.a == 1.0) {
				probeQuery.CommitNonOpaqueTriangleHit();
			} else {
				float absorptionStrength = intersectionColor.a * 2.0;
				float3 absorptionCoefficient = (1.0 - intersectionColor.rgb) * absorptionStrength;
				candidateTransmittanceFactor *= exp(-absorptionCoefficient);
			}
		}
	}

	float3 sampledRadiance;
	float sampledDistance;
	if (probeQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
		VSOutput intersectionPoint = intersectionVSOutput(
			probeQuery.CommittedRayInstanceCustomIndex(),
			probeQuery.CommittedRayPrimitiveIndex(),
			probeQuery.CommittedRayBarycentrics(),
			probeQuery.CommittedRayObjectToWorld(),
			probeQuery.CommittedRayWorldToObject()
		);
		MaterialBuffer currMaterial = materials[intersectionPoint.materialIndex];

		float3 N = normalize(intersectionPoint.normal);
		if (currMaterial.normalTextureIndex != UINT32_MAX) {
			float3 T = normalize(intersectionPoint.tangent.xyz);
			T = normalize(T - dot(T, N) * N);
			float3 B = cross(N, T) * intersectionPoint.tangent.w;
			float3x3 TBN = float3x3(T, B, N);
			float3 mappedNormal =
				textures[currMaterial.normalTextureIndex].SampleLevel(intersectionPoint.UV, 0).xyz *
					2.0 -
				1.0;
			N = normalize(mul(mappedNormal, TBN));
		}
		if (any(isnan(N))) {
			N = { 0, 0, 1 };
		}

		float4 baseColor = currMaterial.baseColorFactor;
		if (currMaterial.baseColorTextureIndex != UINT32_MAX) {
			baseColor *=
				textures[currMaterial.baseColorTextureIndex].SampleLevel(intersectionPoint.UV, 0);
		}

		float metallic = currMaterial.metallicRoughnessFactor.r;
		float roughness = currMaterial.metallicRoughnessFactor.g;
		if (currMaterial.metallicRoughnessTextureIndex != UINT32_MAX) {
			metallic *= textures[currMaterial.metallicRoughnessTextureIndex]
							.SampleLevel(intersectionPoint.UV, 0)
							.r;
			roughness *= textures[currMaterial.metallicRoughnessTextureIndex]
							 .SampleLevel(intersectionPoint.UV, 0)
							 .g;
		}
		float ao = currMaterial.occlusionStrength;
		if (currMaterial.occlusionTextureIndex != UINT32_MAX) {
			ao *=
				textures[currMaterial.occlusionTextureIndex].SampleLevel(intersectionPoint.UV, 0).r;
		}
		float4 intersectionPointRadiance = getPointRadiance(
			intersectionPoint.worldPosition,
			N,
			intersectionPoint.UV,
			baseColor,
			metallic,
			roughness,
			ao,
			currMaterial.alphaMode,
			ddgiProbePositions
			[probeIndex.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
			 probeIndex.y * DDGI_PROBE_DIMENSIONS.z + probeIndex.z]
		);
		sampledRadiance = intersectionPointRadiance.rgb;
		sampledDistance = distance(probeQuery.WorldRayOrigin(), intersectionPoint.worldPosition);

		probeRayDesc.TMax = probeQuery.CommittedRayT();
		rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE;
		probeQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, probeRayDesc);

		float3 transmittanceFactor = float3(1); // accumulate transmittance
		while (probeQuery.Proceed()) {
			uint instanceIndex = probeQuery.CandidateRayInstanceCustomIndex();
			uint primIndex = probeQuery.CandidatePrimitiveIndex();

			float2 uv = intersectionUV(
				instanceIndex, primIndex, probeQuery.CandidateTriangleBarycentrics()
			);

			MaterialBuffer material = materials[tlasLut[instanceIndex].materialIndex];
			float4 intersectionColor =
				textures[NonUniformResourceIndex(material.baseColorTextureIndex)].SampleLevel(
					uv, 0
				) *
				material.baseColorFactor;
			float absorptionStrength = intersectionColor.a * 2.0;
			float3 absorptionCoefficient = (1.0 - intersectionColor.rgb) * absorptionStrength;
			transmittanceFactor *= exp(-absorptionCoefficient);
		}
		sampledRadiance *= transmittanceFactor;
	} else {
		sampledRadiance =
			float3(0) * candidateTransmittanceFactor; // code for sampling sky box goes here
		sampledDistance = 1e4;
	}
	ddgiRadianceTransmissionTexture.Store(
		{ rayIndex.x,
		  probeIndex.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
			  probeIndex.y * DDGI_PROBE_DIMENSIONS.z + probeIndex.z },
		sampledRadiance
	);
	ddgiDistanceTransmissionTexture.Store(
		{ rayIndex.x,
		  probeIndex.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
			  probeIndex.y * DDGI_PROBE_DIMENSIONS.z + probeIndex.z },
		sampledDistance
	);
}

