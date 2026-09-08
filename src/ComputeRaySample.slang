import GraphicsBindings;
import ComputeBindings;
import Lighting;
import Constants;

[shader("compute")]
[numthreads(RAY_COUNT, 1, 1)]
void computeMain(uint3 probeIndex: SV_GroupID, uint3 rayIndex: SV_GroupThreadID) {
	static groupshared uint globalProbeIndex;
	static groupshared uint probeMinimumDistance;
	static groupshared uint probeMaximumDistance;
	static groupshared uint counter;
	static groupshared bool probeDeactivated;
	static groupshared bool clearProbe;
	if (rayIndex.x == 0) {
		globalProbeIndex = probeIndex.x + CASCADE_INDEX * PROBES_PER_CASCADE;
		probeMinimumDistance = UINT32_MAX;
		probeMaximumDistance = 0;
		counter = 0;
		probeDeactivated = false;
		clearProbe = false;
		BoundsState probeState = ddgiBoundsCheck(
			ddgiProbes[globalProbeIndex].position, ddgiCascades[CASCADE_INDEX].innerBounds
		);
		if (probeState == BoundsState::inside) {
			ddgiProbes[globalProbeIndex].state = DDGIProbe::State::inactive;
			probeDeactivated = true;
		} else if (probeState == BoundsState::on && CASCADE_INDEX != 0) {
			ddgiProbes[globalProbeIndex].state = DDGIProbe::State::border;
			probeDeactivated = true;
		} else {
			ddgiProbes[globalProbeIndex].state = DDGIProbe::State::active;
		}
		if (!probeDeactivated) {
			int3 position = indexToGlobalCoordinates(
				probeIndex.x,
				ddgiCascades[CASCADE_INDEX].outerBounds,
				ddgiCascades[CASCADE_INDEX].gridSpacing
			);
			if (any(ddgiProbes[globalProbeIndex].position != position)) {
				ddgiProbes[globalProbeIndex].position = position;
				clearProbe = true;
			}
		}
	}
	GroupMemoryBarrierWithGroupSync();
	if (probeDeactivated) {
		return;
	}

	RayDesc probeRayDesc;
	probeRayDesc.Origin =
		ddgiProbes[globalProbeIndex].position + ddgiProbes[globalProbeIndex].offset;
	probeRayDesc.Direction = ddgiProbeSamples[rayIndex.x];
	probeRayDesc.TMin = EPSILON;
	probeRayDesc.TMax = 1e4;

	RayQuery probeQuery;
	uint rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

	probeQuery.TraceRayInline(accelerationStructure, rayFlags, 0xFF, probeRayDesc);
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
			}
		}
	}

	float3 sampledRadiance;
	float sampledDistance;
	float maxRayDistance = 1.5 * ddgiCascades[CASCADE_INDEX].gridSpacing;
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
			ddgiProbes[globalProbeIndex].position
		);
		sampledRadiance = intersectionPointRadiance.rgb;
		sampledDistance =
			min(maxRayDistance,
				distance(probeQuery.WorldRayOrigin(), intersectionPoint.worldPosition));

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
		sampledRadiance = float3(0); // code for sampling sky box goes here
		sampledDistance = maxRayDistance;
	}
	int2 transmissionIndex =
		int2((CASCADE_INDEX % 2) * RAY_COUNT, (CASCADE_INDEX / 2) * PROBES_PER_CASCADE) +
		int2(rayIndex.x, probeIndex.x);
	ddgiRadianceTransmissionTexture.Store(transmissionIndex, sampledRadiance);
	ddgiDistanceTransmissionTexture.Store(transmissionIndex, sampledDistance);

	uint distanceOffset = uint((sampledDistance / maxRayDistance) * (pow(2, 16) - 1));
	distanceOffset <<= 16;
	distanceOffset |= float3ToR6G5B5(sampledDistance);
	InterlockedMin(probeMinimumDistance, distanceOffset);
	InterlockedMax(probeMaximumDistance, distanceOffset);

	GroupMemoryBarrierWithGroupSync();
	int originalCounterValue;
	InterlockedAdd(counter, 1, originalCounterValue);
	if (originalCounterValue == RAY_COUNT - 1) {
		float minDistance = ((probeMinimumDistance >> 16) / float(pow(2, 16) - 1)) * maxRayDistance;
		float maxDistance = ((probeMaximumDistance >> 16) / float(pow(2, 16) - 1)) * maxRayDistance;
		if (maxDistance < ddgiCascades[CASCADE_INDEX].gridSpacing / 2.f) {
			ddgiProbes[globalProbeIndex].state = DDGIProbe::State::inactive;
			return;
		}
		if (minDistance < MINIMUM_RAY_DISTANCE) {
			ddgiProbes[globalProbeIndex].offset =
				-R6G5B5ToFloat3(probeMinimumDistance) * (MINIMUM_RAY_DISTANCE - minDistance);
			clearProbe = true;
			printf( "Probe offsetted! %f, %f, %f\n", ddgiProbes[globalProbeIndex].offset.x, ddgiProbes[globalProbeIndex].offset.y, ddgiProbes[globalProbeIndex].offset.z);
		}
		if (clearProbe) {
			int originalValue;
			InterlockedAdd(ddgiClearCommand[0].x, 1, originalValue);
			ddgiClearIndices[originalValue] = DDGIClearIndex(probeIndex.x, CASCADE_INDEX);
		}
	}
}
