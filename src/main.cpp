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
	std::unique_ptr<Scene> currentScene {std::make_unique<Scene>("Cornell.glb", renderer)};
	auto* window {renderer.getWindow()};

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}

	double prevXPos {0.0};
	double prevYPos {0.0};
	bool prevF1State {false};
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		int state = glfwGetKey(window, GLFW_KEY_W);
		if (state == GLFW_PRESS) {
			currentScene->moveCameraPosition(0.1f * currentScene->getLookAtVector());
		}
		state = glfwGetKey(window, GLFW_KEY_S);
		if (state == GLFW_PRESS) {
			currentScene->moveCameraPosition(-0.1f * currentScene->getLookAtVector());
		}
		state = glfwGetKey(window, GLFW_KEY_ESCAPE);
		if (state == GLFW_PRESS) {
			break;
		}
#ifndef NDEBUG
		state = glfwGetKey(window, GLFW_KEY_F1);
		if (state == GLFW_PRESS) {
			if (prevF1State == false) {
				renderer.m_drawDdgiProbes = !renderer.m_drawDdgiProbes;
			}
			prevF1State = true;
		} else {
			prevF1State = false;
		}
#endif

		double xPos, yPos;
		glfwGetCursorPos(window, &xPos, &yPos);
		if (xPos != prevXPos or yPos != prevYPos) {
			currentScene->moveCameraGaze(
				{static_cast<float>(xPos - prevXPos) * -0.004,
				 static_cast<float>(yPos - prevYPos) * -0.004}
			);
		}
		prevXPos = xPos;
		prevYPos = yPos;

		renderer.drawFrame();
	}
	renderer.waitIdle();
	return 0;
}
