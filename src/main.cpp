#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#define TINYGLTF_IMPLEMENTATION

#define VMA_IMPLEMENTATION

#define GLFW_INCLUDE_VULKAN

#include "Scene.hpp"
#include "VulkanInterface.hpp"

int main() {
	VulkanInterface renderer {};
	std::unique_ptr<Scene> currentScene {std::make_unique<Scene>("Scene1.glb", renderer)};
	auto* window {renderer.getWindow()};

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	double prevXPos {0.0};
	double prevYPos {0.0};
	while (!glfwWindowShouldClose(window)) {
		Camera& camera {currentScene->sceneCamera};
		glfwPollEvents();
		int state = glfwGetKey(window, GLFW_KEY_W);
		if (state == GLFW_PRESS) {
			camera.moveCameraPosition(0.1f * camera.getLookAtVector());
		}
		state = glfwGetKey(window, GLFW_KEY_S);
		if (state == GLFW_PRESS) {
			camera.moveCameraPosition(-0.1f * camera.getLookAtVector());
		}
		state = glfwGetKey(window, GLFW_KEY_ESCAPE);
		if (state == GLFW_PRESS) {
			break;
		}

		double xPos, yPos;
		glfwGetCursorPos(window, &xPos, &yPos);
		camera.moveCameraGaze(
			{static_cast<float>(xPos - prevXPos) * -0.004,
			 static_cast<float>(yPos - prevYPos) * -0.004}
		);
		prevXPos = xPos;
		prevYPos = yPos;

		renderer.drawFrame();
	}
	renderer.waitIdle();
	return 0;
}
