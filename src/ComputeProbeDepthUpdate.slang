import bindings;

[shader("compute")]
[numthreads(14, 14, 1)]
void computeMain(uint3 probeID: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared uint counter;
	static groupshared int probeIndex;
	static groupshared int2 texelID;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		counter = 0;
		probeIndex = probeID.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
					 probeID.y * DDGI_PROBE_DIMENSIONS.z + probeID.z;
		texelID = { (probeIndex % 64) * 16 + 1, (probeIndex / 64) * 16 + 1 };
	}
	GroupMemoryBarrierWithGroupSync();

	uint previousSampleCount = ddgiDepthSampleCountTexture.Load(texelID + localTexelUV.xy);

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

	int index = RAY_COUNT - int((texelVector.z + 1) / (2.0 / RAY_COUNT)) - 1;

	float distance = 0;
	float distance2 = 0;
	int sampleCount = 0;
	int status = 0;
	for (int currIndex = index; currIndex < index + 2; ++currIndex) {
		float dot = dot(texelVector, ddgiProbeSamples[currIndex]);
		if (dot >= 0.99999326) {
			++status;
			float currDistance = ddgiDistanceTransmissionTexture.Load( { currIndex, probeIndex });
			distance += currDistance;
			distance2 += currDistance * currDistance;
			++sampleCount;
		} else if (status == 1) {
			break;
		}
	}
	if (previousSampleCount + sampleCount <= UINT32_MAX) {
		float2 previousDistances = ddgiDepthTexture.Load(texelID + localTexelUV.xy);
		ddgiDepthTexture.Store(
			texelID + localTexelUV.xy,
			{ distance + previousDistances.x, distance2 + previousDistances.y }
		);
		ddgiDepthSampleCountTexture.Store(
			texelID + localTexelUV.xy, sampleCount + previousSampleCount
		);
	}

	int previousValue;
	InterlockedAdd(counter, 1, previousValue);
	if (previousValue == 195) {
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
		ddgiDepthTexture.Store(texelID + int2(14, 14), ddgiDepthTexture.Load(texelID));
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
