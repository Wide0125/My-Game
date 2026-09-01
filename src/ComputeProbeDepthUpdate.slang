import bindings;

[shader("compute")]
[numthreads(14, 14, 1)]
void computeMain(uint3 probeIndex: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared uint counter;
	static groupshared int probeIndex;
	static groupshared int2 texelID;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		counter = 0;
		texelID =
			int2(
				((probeIndex.x / PROBES_PER_CASCADE) % 2) * DDGI_TEXTURE_SIZE * 16,
				((probeIndex.x / PROBES_PER_CASCADE) / 2) * DDGI_TEXTURE_SIZE * 16
			) +
			int2((probeIndex.x % DDGI_TEXTURE_SIZE) * 16, (probeIndex.x / DDGI_TEXTURE_SIZE) * 16);
		;
	}
	GroupMemoryBarrierWithGroupSync();

	float texelX = localTexelUV.x - 6.5;
	float texelY = localTexelUV.y - 6.5;
	float3 texelVector = { texelX / 7, texelY / 7 };
	if (abs(texelX) + abs(texelY) > 7) {
		texelVector.z = -(1.f - abs(texelVector.x) - abs(texelVector.y));
		texelVector.x = (1.f - abs(texelVector.y)) * sign(texelVector.x);
		texelVector.y = (1.f - abs(texelVector.x)) * sign(texelVector.y);
	} else if (abs(texelX) + abs(texelY) == 7) {
		texelVector.z = 0.f;
	} else {
		texelVector.z = 1.f - abs(texelVector.x) - abs(texelVector.y);
	}
	texelVector = normalize(texelVector);

	float distance = 0;
	float distance2 = 0;
	float accumulatedDots = 0;
	int status = 0;
	int2 offsetUV = int2(
		((probeIndex.x / PROBES_PER_CASCADE) % 2) * RAY_COUNT,
		((probeIndex.x / PROBES_PER_CASCADE) / 2) * PROBES_PER_CASCADE
	);
	int index = RAY_COUNT - int((texelVector.z + 1) / (2.f / RAY_COUNT)) - 4;
	for (int currIndex = index; currIndex <= index + 8; ++currIndex) {
		float dot = dot(texelVector, ddgiProbeSamples[currIndex]);
		if (dot >= 0.8) {
			++status;
			float currDistance =
				ddgiDistanceTransmissionTexture.Load(offsetUV + int2(currIndex, probeIndex.x % PROBES_PER_CASCADE));
			distance += currDistance * dot;
			distance2 += currDistance * currDistance * dot;
			accumulatedDots += dot;
		} else if (status > 0) {
			break;
		}
	}
	if (accumulatedDots > 0) {
		distance /= accumulatedDots;
		distance2 /= accumulatedDots;
		float2 previousDistances = ddgiDepthTexture.Load(texelID + localTexelUV.xy);
		ddgiDepthTexture.Store(
			texelID + localTexelUV.xy,
			{ previousDistances.x * HYSTERESIS + distance * (1 - HYSTERESIS),
			  previousDistances.y * HYSTERESIS + distance2 * (1 - HYSTERESIS) }
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
