public module ComputeBindings;

[vk::push_constant]
uint CASCADE_INDEX;

struct Bounds {
	int3 upperBounds;
	int3 lowerBounds;
};
struct DDGICascade {
	int gridSpacing;
	Bounds outerBounds;
	Bounds innerBounds;
};

struct DDGIProbe {
	int3 position;
	bool active;
	float3 offset;
};

struct DDGIClearIndex {
	uint probeIndex;
	uint cascadeIndex;
}

struct DispatchIndirectCommand {
	uint x;
	uint y;
	uint z;
}

[vk::binding(10, 0)]
RWStructuredBuffer<DDGIProbe> ddgiProbes;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(11, 0)]
Sampler2D ddgiIrradianceSampler;
[vk::image_format("rg16f")]
[vk::binding(12, 0)]
Sampler2D ddgiDepthSampler;
[vk::binding(13, 0)]
StructuredBuffer<float3> ddgiProbeSamples;
[vk::binding(14, 0)]
StructuredBuffer<DDGICascade> ddgiCascades;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(15, 0)]
RWTexture2D<float3> ddgiRadianceTransmissionTexture;
[vk::image_format("r16f")]
[vk::binding(16, 0)]
RWTexture2D<float> ddgiDistanceTransmissionTexture;
[vk::image_format("r11f_g11f_b10f")]
[vk::binding(17, 0)]
RWTexture2D<float3> ddgiIrradianceTexture;
[vk::image_format("rg16f")]
[vk::binding(18, 0)]
RWTexture2D<float2> ddgiDepthTexture;
[vk::binding(19, 0)]
ConstantBuffer<int3> DDGI_BASE_DIMENSIONS;
[vk::binding(20, 0)]
RWStructuredBuffer<DDGIClearIndex> ddgiClearIndices;
[vk::binding(21, 0)]
RWStructuredBuffer<DispatchIndirectCommand> ddgiClearCommand;

bool withinBounds(int3 position, Bounds bounds) {
	bool3 less = position <= bounds.upperBounds;
	bool3 greater = position >= bounds.lowerBounds;
	return all(less) && all(greater);
}

int3 indexToGlobalCoordinates(int index, Bounds cascadeBounds, int gridSpacing) {
	int3 stableCoordinate;
	stableCoordinate.x = index / ((DDGI_BASE_DIMENSIONS.y + 1) * (DDGI_BASE_DIMENSIONS.z + 1));
	index -= stableCoordinate.x * ((DDGI_BASE_DIMENSIONS.y + 1) * (DDGI_BASE_DIMENSIONS.z + 1));
	stableCoordinate.y = index / (DDGI_BASE_DIMENSIONS.z + 1);
	index -= stableCoordinate.y * (DDGI_BASE_DIMENSIONS.z + 1);
	stableCoordinate.z = index;

	int3 upperStable = globalToStableCoordinates(cascadeBounds.upperBounds, gridSpacing);
	int3 difference = upperStable - stableCoordinate;
	if (difference.x < 0) {
		difference.x += DDGI_BASE_DIMENSIONS.x + 1;
	}
	if (difference.y < 0) {
		difference.y += DDGI_BASE_DIMENSIONS.y + 1;
	}
	if (difference.z < 0) {
		difference.z += DDGI_BASE_DIMENSIONS.z + 1;
	}
	return cascadeBounds.upperBounds - difference * gridSpacing;
} // local index within cascade

int3 globalToStableCoordinates(int3 position, int gridSpacing) {
	int3 newVector =
		(position / gridSpacing + DDGI_BASE_DIMENSIONS / 2) % (DDGI_BASE_DIMENSIONS + int3(1));
	if (newVector.x < 0) {
		newVector.x += DDGI_BASE_DIMENSIONS.x + 1;
	}
	if (newVector.y < 0) {
		newVector.y += DDGI_BASE_DIMENSIONS.y + 1;
	}
	if (newVector.z < 0) {
		newVector.z += DDGI_BASE_DIMENSIONS.z + 1;
	}
	return newVector;
}

uint float3ToR6G5B5(float3 input) {
	input = min(float3(1), input);
	uint x = uint(input.x * (pow(2, 6) - 1));
	x <<= 10;
	uint y = uint(input.y * (pow(2, 5) - 1));
	y <<= 5;
	uint z = uint(input.z * (pow(2, 5) - 1));

	return x | y | z;
} // packs a float3 into r6g5b5 format using fixed point (assumes float3 is normalized, clamps if
  // otherwise)

float3 R6G5B5ToFloat3(uint input) {
	uint xMask = 0b00000000000000001111110000000000;
	uint yMask = 0b00000000000000000000001111100000;
	uint zMask = 0b00000000000000000000000000011111;

	uint x = input & xMask;
	x >>= 10;
	uint y = input & yMask;
	y >>= 5;
	uint z = input & zMask;

	float3 output;
	output.x = x / (pow(2, 6) - 1);
	output.y = y / (pow(2, 5) - 1);
	output.z = z / (pow(2, 5) - 1);
	return output;
} // unpacks a r6g5b5 uint into a float3, discarding the leftmost 16 bits
