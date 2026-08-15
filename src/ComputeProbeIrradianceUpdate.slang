import bindings;

[shader("compute")]
[numthreads(6, 6, 1)]
void computeMain(uint3 probeID: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared uint counter;
	static groupshared int probeIndex;
	static groupshared int2 texelID;
	static groupshared uint nanCounter;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		counter = 0;
		probeIndex = probeID.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
					 probeID.y * DDGI_PROBE_DIMENSIONS.z + probeID.z;
		texelID = { (probeIndex % 64) * 8 + 1, (probeIndex / 64) * 8 + 1 };
		nanCounter = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	float texelX = localTexelUV.x - 2.5;
	float texelY = localTexelUV.y - 2.5;
	float3 texelVector = { texelX / 3.f, texelY / 3.f };
	if (abs(texelX) + abs(texelY) > 3) {
		texelVector.z = -(1 - abs(texelVector.x) - abs(texelVector.y));
		texelVector.x = (1.0 - abs(texelVector.y)) * sign(texelVector.x);
		texelVector.y = (1.0 - abs(texelVector.x)) * sign(texelVector.y);
	} else if (abs(texelX) + abs(texelY) == 3) {
		texelVector.z = 0;
	} else {
		texelVector.z = 1 - abs(texelVector.x) - abs(texelVector.y);
	}
	texelVector = normalize(texelVector);

	float3 irradiance = float3(0);
	float weightSum = float(0);
	int rayCount = 0;
	for (int x = 0; x < RAY_COUNT; ++x) {
		float dotProduct = dot(texelVector, ddgiProbeSamples[x]);
		if (dotProduct >= 0) {
			rayCount++;
			irradiance += ddgiRadianceTransmissionTexture.Load( { x, probeIndex }) * dotProduct;
			weightSum += dotProduct;
		}
	}
	irradiance *= PI / rayCount;
	float3 oldIrradiance = ddgiIrradianceTexture.Load(texelID + localTexelUV.xy);
	ddgiIrradianceTexture.Store(
		texelID + localTexelUV.xy, oldIrradiance * HYSTERESIS + irradiance * (1 - HYSTERESIS)
	);

	int previousValue;
	InterlockedAdd(counter, 1, previousValue);
	if (previousValue == 35) {
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(6, i), ddgiIrradianceTexture.Load(texelID + int2(5, 5 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(6, 3 + i), ddgiIrradianceTexture.Load(texelID + int2(5, 2 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(-1, i), ddgiIrradianceTexture.Load(texelID + int2(0, 5 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(-1, 3 + i), ddgiIrradianceTexture.Load(texelID + int2(0, 2 - i))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(i, 6), ddgiIrradianceTexture.Load(texelID + int2(5 - i, 5))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(3 + i, 6), ddgiIrradianceTexture.Load(texelID + int2(2 - i, 5))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(i, -1), ddgiIrradianceTexture.Load(texelID + int2(5 - i, 0))
			);
		}
		for (int i = 0; i < 3; ++i) {
			ddgiIrradianceTexture.Store(
				texelID + int2(3 + i, -1), ddgiIrradianceTexture.Load(texelID + int2(2 - i, 0))
			);
		}
		ddgiIrradianceTexture.Store(texelID + int2(6, 6), ddgiIrradianceTexture.Load(texelID));
		ddgiIrradianceTexture.Store(
			texelID + int2(-1, -1), ddgiIrradianceTexture.Load(texelID + int2(5, 5))
		);
		ddgiIrradianceTexture.Store(
			texelID + int2(6, -1), ddgiIrradianceTexture.Load(texelID + int2(0, 5))
		);
		ddgiIrradianceTexture.Store(
			texelID + int2(-1, 6), ddgiIrradianceTexture.Load(texelID + int2(5, 0))
		);
	}
}
