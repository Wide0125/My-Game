import bindings;

[shader("compute")]
[numthreads(RAY_COUNT, 1, 1)]
void computeMain(uint3 probeID: SV_GroupID, uint3 rayIndex: SV_GroupThreadID) {
	static groupshared Atomic<uint> counter;
	static groupshared Atomic<uint> rayCounter[196];
	if (rayIndex.x == 0) {
		counter = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	int probeIndex = probeID.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
					 probeID.y * DDGI_PROBE_DIMENSIONS.z + probeID.z;

	float3 rayVector = ddgiProbeSamples[rayIndex.x];
	float3 octaVector = rayVector / (abs(rayVector.x) + abs(rayVector.y) + abs(rayVector.z));

	float2 UV;
	if (rayVector.z < 0) {
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
	int2 texelID = { (probeIndex % 64) * 16 + 1, (probeIndex / 64) * 16 + 1 };
	float2 accumulatedDepth = ddgiDepthTexture.Load(texelID + texelUV);
	float distance = ddgiDistanceTransmissionTexture.Load( { probeIndex, rayIndex.x });
	ddgiDepthTexture.Store(
		texelID + texelUV, accumulatedDepth + float2(distance, distance * distance)
	);

	rayCounter[texelUV.x * 14 + texelUV.y].increment();

	++counter;
	if (counter.increment() == RAY_COUNT - 1) {
		for (int x = 0; x < 14; ++x) {
			for (int y = 0; y < 14; ++y) {
				float2 depth =
					ddgiDepthTexture.Load(texelID + int2(x, y)) / rayCounter[x * 14 + y].load();
				ddgiDepthTexture.Store(texelID + int2(x, y), depth);
			}
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(14, i), ddgiDepthTexture.Load(texelID + int2(13, 13 - i))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(14, 7 + i), ddgiDepthTexture.Load(texelID + int2(13, 6 - i))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(-1, i), ddgiDepthTexture.Load(texelID + int2(0, 13 - i))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(-1, 7 + i), ddgiDepthTexture.Load(texelID + int2(0, 6 - i))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(i, 14), ddgiDepthTexture.Load(texelID + int2(13 - i, 13))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(7 + i, 14), ddgiDepthTexture.Load(texelID + int2(6 - i, 13))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(i, -1), ddgiDepthTexture.Load(texelID + int2(13 - i, 0))
			);
		}
		for (int i = 0; i < 7; ++i) {
			ddgiDepthTexture.Store(
				texelID + int2(7 + i, -1), ddgiDepthTexture.Load(texelID + int2(6 - i, 0))
			);
		}
		ddgiDepthTexture.Store(
			texelID + int2(14, 14), ddgiDepthTexture.Load(texelID)
		);
		ddgiDepthTexture.Store(
			texelID + int2(-1, -1), ddgiDepthTexture.Load(texelID + int2(13, 13))
		);
		ddgiDepthTexture.Store(
			texelID + int2(14, -1), ddgiDepthTexture.Load(texelID + int2(0, 13))
		);
		ddgiDepthTexture.Store(
			texelID + int2(-1, 14), ddgiDepthTexture.Load(texelID + int2(13, 0))
		);
	}
}
