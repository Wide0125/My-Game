import ComputeBindings;
import Constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 workgroupIndex: SV_GroupID, uint3 localTexelUV: SV_GroupThreadID) {
	static groupshared int2 texelID;
	if (localTexelUV.x == 0 && localTexelUV.y == 0) {
		texelID = int2(
					  (ddgiClearIndices[workgroupIndex.x].cascadeIndex % 2) * DDGI_TEXTURE_SIZE * 8,
					  (ddgiClearIndices[workgroupIndex.x].cascadeIndex / 2) * DDGI_TEXTURE_SIZE * 8
				  ) +
				  int2(
					  (ddgiClearIndices[workgroupIndex.x].probeIndex % DDGI_TEXTURE_SIZE) * 8,
					  (ddgiClearIndices[workgroupIndex.x].probeIndex / DDGI_TEXTURE_SIZE) * 8
				  );
	}
	GroupMemoryBarrierWithGroupSync();

	ddgiIrradianceTexture.Store(texelID + localTexelUV.xy, float3(0));
}
