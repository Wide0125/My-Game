import bindings;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 probeID: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared int probeIndex;
	static groupshared int2 texelID;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		probeIndex = probeID.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
					 probeID.y * DDGI_PROBE_DIMENSIONS.z + probeID.z;
		texelID = { (probeIndex % 64) * 8, (probeIndex / 64) * 8 };
	}
	GroupMemoryBarrierWithGroupSync();

	ddgiIrradianceTexture.Store(texelID + localTexelUV.xy, { 0, 0, 0 });
}
