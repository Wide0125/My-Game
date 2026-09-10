#include "PRNG.h"
#include <glm/glm.hpp>
#include <print>

uint32_t float3ToR8G8B8(glm::vec3 input) {
	input = min(glm::vec3(1), input);
	uint32_t x = uint32_t(input.x * (pow(2, 7) - 1));
	x <<= 16;
	if (input.x < 0) {
		x |= 0b00000000100000000000000000000000;
	}
	uint32_t y = uint32_t(input.y * (pow(2, 7) - 1));
	y <<= 8;
	if (input.y < 0) {
		y |= 0b00000000000000001000000000000000;
	}
	uint32_t z = uint32_t(input.z * (pow(2, 7) - 1));
	if (input.z < 0) {
		z |= 0b00000000000000000000000010000000;
	}

	return x | y | z;
} // packs a glm::vec3 into r8g8b8 format using fixed point (assumes glm::vec3 is normalized, clamps if
  // otherwise)

glm::vec3 R8G8B8ToFloat3(uint32_t input) {
	uint32_t xMask = 0b00000000011111110000000000000000;
	uint32_t xSignMask = 0b00000000100000000000000000000000;
	uint32_t yMask = 0b00000000000000000111111100000000;
	uint32_t ySignMask = 0b00000000000000001000000000000000;
	uint32_t zMask = 0b00000000000000000000000001111111;
	uint32_t zSignMask = 0b00000000000000000000000010000000;

	uint32_t x = (input & xMask) >> 16;
	uint32_t y = (input & yMask) >> 8;
	uint32_t z = input & zMask;

	glm::vec3 output;
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
} // unpacks a r8g8b8 uint32_t into a glm::vec3, discarding the leftmost 16 bits

int main() {
	for (int i {0}; i < 1000; ++i) {
		PRNG rng;
		glm::vec3 direction {};
		direction.x = rng.getRandomFloat();
		direction.y = rng.getRandomFloat();
		direction.z = rng.getRandomFloat();

		int maxDistance {8};
		float distance {rng.getRandomFloat()};
		uint32_t packedDistance {
			static_cast<uint32_t>(std::round((distance / maxDistance) * (pow(2, 8) - 1))) << 24
		};

		uint32_t packedDirection {float3ToR8G8B8(direction)};

		uint32_t packedInt {packedDistance | packedDirection};

		float unpackedDistance {((packedInt >> 24) / float(pow(2, 8) - 1)) * maxDistance};
		glm::vec3 unpackedDirection {R8G8B8ToFloat3(packedInt)};

		std::println(
			"Original Distance: {} \nOriginal Direction: {}, {}, {} \nUnpacked Distance "
			": {} \nUnpacked "
			"Direction: {}, {}, {} \n \n",
			distance,
			direction.x,
			direction.y,
			direction.z,
			unpackedDistance,
			unpackedDirection.x,
			unpackedDirection.y,
			unpackedDirection.z
		);
	};

	return 0;
}
