import ComputeBindings;
import Constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 probeIndex: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared int2 texelID;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		texelID =
			int2(
				(cascadeIndex % 2) * DDGI_TEXTURE_SIZE * 8,
				(cascadeIndex / 2) * DDGI_TEXTURE_SIZE * 8
			) +
			int2((probeIndex.x % DDGI_TEXTURE_SIZE) * 8, (probeIndex.x / DDGI_TEXTURE_SIZE) * 8);
	}
	GroupMemoryBarrierWithGroupSync();

	ddgiIrradianceTexture.Store(texelID + localTexelUV.xy, { 0, 0, 0 });
}
