public module ComputeBindings;

[[vk::push_constant]]
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
	enum State { active, inactive, border, relocated };
	State state;
	float3 offset;
	bool relocated;
};

[[vk::binding(10, 0)]]
RWStructuredBuffer<DDGIProbe> ddgiProbes;
[[vk::image_format("r11f_g11f_b10f")]]
[[vk::binding(11, 0)]]
Sampler2D ddgiIrradianceSampler;
[[vk::image_format("rg16f")]]
[[vk::binding(12, 0)]]
Sampler2D ddgiDepthSampler;
[[vk::binding(13, 0)]]
StructuredBuffer<float3> ddgiProbeSamples;
[[vk::binding(14, 0)]]
StructuredBuffer<DDGICascade> ddgiCascades;
[[vk::image_format("r11f_g11f_b10f")]]
[[vk::binding(15, 0)]]
RWTexture2D<float3> ddgiRadianceTransmissionTexture;
[[vk::image_format("r16f")]]
[[vk::binding(16, 0)]]
RWTexture2D<float> ddgiDistanceTransmissionTexture;
[[vk::image_format("r11f_g11f_b10f")]]
[[vk::binding(17, 0)]]
RWTexture2D<float3> ddgiIrradianceTexture;
[[vk::image_format("rg16f")]]
[[vk::binding(18, 0)]]
RWTexture2D<float2> ddgiDepthTexture;
[[vk::binding(19, 0)]]
ConstantBuffer<int3> DDGI_BASE_DIMENSIONS;

enum BoundsState { inside, on, outside }
BoundsState ddgiBoundsCheck(float3 position, Bounds bounds) {
	bool3 less = position < bounds.upperBounds;
	bool3 greater = position > bounds.lowerBounds;
	if (all(less) && all(greater)) {
		return BoundsState::inside;
	} else {
		greater = position > bounds.upperBounds;
		less = position < bounds.lowerBounds;
		if (any(less) || any(greater)) {
			return BoundsState::outside;
		} else {
			return BoundsState::on;
		}
	}
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

uint float3ToR8G8B8(float3 input) {
	input = min(float3(1), input);
	uint x = uint(abs(input.x) * (pow(2, 7) - 1));
	x <<= 16;
	if (input.x < 0) {
		x |= 0b00000000100000000000000000000000;
	}
	uint y = uint(abs(input.y) * (pow(2, 7) - 1));
	y <<= 8;
	if (input.y < 0) {
		y |= 0b00000000000000001000000000000000;
	}
	uint z = uint(abs(input.z) * (pow(2, 7) - 1));
	if (input.z < 0) {
		z |= 0b00000000000000000000000010000000;
	}

	return x | y | z;
} // packs a float3 into r8g8b8 format using fixed point (assumes float3 is normalized, clamps if
  // otherwise)

float3 R8G8B8ToFloat3(uint input) {
	uint xMask =     0b00000000011111110000000000000000;
	uint xSignMask = 0b00000000100000000000000000000000;
	uint yMask =     0b00000000000000000111111100000000;
	uint ySignMask = 0b00000000000000001000000000000000;
	uint zMask =     0b00000000000000000000000001111111;
	uint zSignMask = 0b00000000000000000000000010000000;

	uint x = (input & xMask) >> 16;
	uint y = (input & yMask) >> 8;
	uint z = input & zMask;

	float3 output;
	output.x = x / float(pow(2, 7) - 1);
	if ((input & xSignMask) == xSignMask) {
		output.x *= -1;
	}
	output.y = y / float(pow(2, 7) - 1);
	if ((input & ySignMask) == ySignMask) {
		output.y *= -1;
	}
	output.z = z / float(pow(2, 7) - 1);
	if ((input & zSignMask) == zSignMask) {
		output.z *= -1;
	}
	return output;
} // unpacks a r8g8b8 uint into a float3, discarding the leftmost 16 bits
