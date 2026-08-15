import bindings;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 probeID: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared int probeIndex;
	static groupshared int2 texelID;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		probeIndex = probeID.x * DDGI_PROBE_DIMENSIONS.y * DDGI_PROBE_DIMENSIONS.z +
					 probeID.y * DDGI_PROBE_DIMENSIONS.z + probeID.z;
		texelID = { (probeIndex % 64) * 16, (probeIndex / 64) * 16 };
	}
	GroupMemoryBarrierWithGroupSync();

	ddgiDepthTexture.Store(texelID + localTexelUV.xy, { 0, 0 });
	ddgiDepthSampleCountTexture.Store(texelID + localTexelUV.xy, { 0 });
}
