import bindings;

[shader("compute")]
[numthreads(6, 6, 1)]
void computeMain(uint3 probeID: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared Atomic<uint> counter;

	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		counter = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	int probeIndex = probeID.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
					 probeID.y * DDGI_PROBE_DIMENSIONS.z + probeID.z;

	float texelX = localTexelUV.x - 2.5;
	float texelY = localTexelUV.y - 2.5;
	float3 texelVector = { texelX / 3, texelY / 3 };
	if (abs(texelX) + abs(texelY) > 3) {
		texelVector.z = -(1 - abs(texelVector.x) - abs(texelVector.y));
	} else if (abs(texelX) + abs(texelY) == 3) {
		texelVector.z = 0;
	} else {
		texelVector.z = 1 - abs(texelVector.x) - abs(texelVector.y);
	}
	texelVector = normalize(texelVector);

	float3 irradiance = float3(0);
	for (int x = 0; x < RAY_COUNT; ++x) {
		float dotProduct = dot(texelVector, ddgiProbeSamples[x]);
		if (dotProduct > 0) {
			irradiance += ddgiRadianceTransmissionTexture.Load( { x, probeIndex }) * dotProduct;
		}
	}
	int2 texelID = { (probeIndex % 64) * 8 + 1, (probeIndex / 64) * 8 + 1 };
	float3 oldIrradiance = ddgiIrradianceTexture.Load(texelID + localTexelUV.xy);
	ddgiIrradianceTexture.Store(texelID + localTexelUV.xy, lerp(oldIrradiance, irradiance, 1 - HYSTERESIS));

	if (counter.increment() == 35) {
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(6, i), ddgiDepthTexture.Load(texelID + int2(5, 5 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(6, 3 + i), ddgiDepthTexture.Load(texelID + int2(4, 2 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(-1, i), ddgiDepthTexture.Load(texelID + int2(0, 5 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(-1, 3 + i), ddgiDepthTexture.Load(texelID + int2(0, 2 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(i, 6), ddgiDepthTexture.Load(texelID + int2(5 - i, 5))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(3 + i, 6), ddgiDepthTexture.Load(texelID + int2(2 - i, 5))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(i, -1), ddgiDepthTexture.Load(texelID + int2(5 - i, 0))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(3 + i, -1), ddgiDepthTexture.Load(texelID + int2(2 - i, 0))
			);
		}
		ddgiDepthTexture.Store(
			texelID + int2(6, 6), ddgiDepthTexture.Load(texelID)
		);
		ddgiDepthTexture.Store(
			texelID + int2(-1, -1), ddgiDepthTexture.Load(texelID + int2(5, 5))
		);
		ddgiDepthTexture.Store(
			texelID + int2(6, -1), ddgiDepthTexture.Load(texelID + int2(0, 5))
		);
		ddgiDepthTexture.Store(
			texelID + int2(-1, 6), ddgiDepthTexture.Load(texelID + int2(5, 0))
		);
	}
}
