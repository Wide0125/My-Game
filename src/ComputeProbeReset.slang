import ComputeBindings;
import Constants;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 probeIndex: SV_GroupID, uint cascadeIndex: SV_GroupIndex) {
	uint globalProbeIndex = probeIndex.x + cascadeIndex * PROBES_PER_CASCADE;
	ddgiProbes[globalProbeIndex].relocated = false;
}
