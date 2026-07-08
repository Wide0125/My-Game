#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#define TINYGLTF_IMPLEMENTATION

#define VMA_IMPLEMENTATION

#define GLFW_INCLUDE_VULKAN

#include "Scene.hpp"
#include "VulkanInterface.hpp"

static void curserOverWindowCallback(GLFWwindow* window, double xpos, double ypos) {
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

int main() {
	VulkanInterface renderer {};
	std::unique_ptr<Scene> currentScene {std::make_unique<Scene>("Scene1.glb", renderer)};

	glfwSetCursorPosCallback(renderer.getWindow(), curserOverWindowCallback);

	double prevXPos {0.0};
	double prevYPos {0.0};
	while (!glfwWindowShouldClose(renderer.getWindow())) {
		Camera& camera {currentScene->sceneCamera};
		glfwPollEvents();
		int state = glfwGetKey(renderer.getWindow(), GLFW_KEY_W);
		if (state == GLFW_PRESS) {
			camera.moveCameraPosition(0.1f * camera.getLookAtVector());
		}
		state = glfwGetKey(renderer.getWindow(), GLFW_KEY_S);
		if (state == GLFW_PRESS) {
			camera.moveCameraPosition(-0.1f * camera.getLookAtVector());
		}

		double xPos, yPos;
		glfwGetCursorPos(renderer.getWindow(), &xPos, &yPos);
		camera.moveCameraGaze(
			{static_cast<float>(xPos - prevXPos) * -0.005,
			 static_cast<float>(yPos - prevYPos) * -0.005}
		);
		prevXPos = xPos;
		prevYPos = yPos;

		renderer.drawFrame();
	}
	renderer.waitIdle();
	return 0;
}
