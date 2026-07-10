#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>

#include "Scene.hpp"
#include "VulkanInterface.hpp"

void VulkanInterface::drawFrame() {
	auto fenceResult =
		m_device.waitForFences(*m_inFlightFences[m_frameIndex], vk::True, UINT64_MAX);
	if (fenceResult != vk::Result::eSuccess) {
		throw std::runtime_error("failed to wait for fence!");
	}
	auto [result, imageIndex] = m_swapChain.acquireNextImage(
		UINT64_MAX, *m_presentCompleteSemaphores[m_frameIndex], nullptr
	);
	if (result == vk::Result::eErrorOutOfDateKHR) {
		recreateSwapChain();
		return;
	}
	if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
		assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
		throw std::runtime_error("failed to acquire swap chain image!");
	}

	m_device.resetFences(*m_inFlightFences[m_frameIndex]); // check fences

	auto& commandBuffer {m_commandBuffers[m_frameIndex]}; // per-draw commands

	commandBuffer.reset();
	commandBuffer.begin({});
	// Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
	transition_image_layout(
		m_swapChainImages[imageIndex],
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eColorAttachmentOptimal,
		{}, // srcAccessMask (no need to wait for previous operations)
		vk::AccessFlagBits2::eColorAttachmentWrite,			// dstAccessMask
		vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
		vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage
		vk::ImageAspectFlagBits::eColor
	);
	// Transition depth image to depth attachment optimal layout
	transition_image_layout(
		*m_depthImage,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eDepthAttachmentOptimal,
		vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
		vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
		vk::PipelineStageFlagBits2::eEarlyFragmentTests |
			vk::PipelineStageFlagBits2::eLateFragmentTests,
		vk::PipelineStageFlagBits2::eEarlyFragmentTests |
			vk::PipelineStageFlagBits2::eLateFragmentTests,
		vk::ImageAspectFlagBits::eDepth
	);

	vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
	vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

	vk::RenderingAttachmentInfo colorAttachmentInfo = {
		.imageView = m_swapChainImageViews[imageIndex],
		.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.clearValue = clearColor
	};

	vk::RenderingAttachmentInfo depthAttachmentInfo = {
		.imageView = m_depthImageView,
		.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eDontCare,
		.clearValue = clearDepth
	};

	vk::RenderingInfo renderingInfo = {
		.renderArea = {.offset = {0, 0}, .extent = m_swapChainExtent},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentInfo,
		.pDepthAttachment = &depthAttachmentInfo
	};

	commandBuffer.beginRendering(renderingInfo);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_graphicsPipeline);
	commandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		m_pipelineLayout,
		0,
		*m_descriptorSets[m_frameIndex],
		nullptr
	);
	commandBuffer.setViewport(
		0,
		vk::Viewport(
			0.0f,
			0.0f,
			static_cast<float>(m_swapChainExtent.width),
			static_cast<float>(m_swapChainExtent.height),
			0.0f,
			1.0f
		)
	);
	commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), m_swapChainExtent));

	std::vector<LightBufferObject> lights {};
	lights.reserve(m_lightCount);
	for (const auto& parentNode: m_currentScene->getParentNodes()) {
		
	}

	uint32_t modelInstanceNum {0};
	for (const auto& parentNode: m_currentScene->getParentNodes()) { // draw models
		const ModelInstance* modelInstancePtr {dynamic_cast<const ModelInstance*>(parentNode)};
		if (modelInstancePtr != nullptr) {
			queueDrawModelInstance(modelInstancePtr, glm::identity<glm::mat4>(), modelInstanceNum);
			++modelInstanceNum;
		}
	}

	commandBuffer.endRendering();
	transition_image_layout(
		m_swapChainImages[imageIndex],
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::ePresentSrcKHR,
		vk::AccessFlagBits2::eColorAttachmentWrite,			// srcAccessMask
		{},													// dstAccessMask
		vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
		vk::PipelineStageFlagBits2::eBottomOfPipe,			// dstStage
		vk::ImageAspectFlagBits::eColor
	);
	commandBuffer.end();

	vk::PipelineStageFlags waitDestinationStageMask(
		vk::PipelineStageFlagBits::eColorAttachmentOutput
	);
	const vk::SubmitInfo submitInfo {
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &*m_presentCompleteSemaphores[m_frameIndex],
		.pWaitDstStageMask = &waitDestinationStageMask,
		.commandBufferCount = 1,
		.pCommandBuffers = &*m_commandBuffers[m_frameIndex],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &*m_renderFinishedSemaphores[imageIndex]
	};
	m_queue.submit(submitInfo, *m_inFlightFences[m_frameIndex]);

	const vk::PresentInfoKHR presentInfoKHR {
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &*m_renderFinishedSemaphores[imageIndex],
		.swapchainCount = 1,
		.pSwapchains = &*m_swapChain,
		.pImageIndices = &imageIndex
	};
	result = m_queue.presentKHR(presentInfoKHR);
	if (!(result == vk::Result::eSuccess or result == vk::Result::eSuboptimalKHR or
		  result == vk::Result::eErrorOutOfDateKHR)) {
		throw std::runtime_error("Failed to present!");
	}

	m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanInterface::queueDrawModelInstance(
	const ModelInstance* modelInstance, const glm::mat4& globalTransform, uint32_t& modelInstanceNum
) {

	const auto& currMeshBuffers {*modelInstance->mesh};

	// update uniform buffer
	MVPBufferObject ubo {};
	glm::mat4 localTransform {
		glm::translate(modelInstance->position) * glm::toMat4(modelInstance->rotation) *
		glm::scale(modelInstance->scale)
	};

	if (modelInstance != nullptr) {
		ubo.model = globalTransform * localTransform;
		const glm::vec3& cameraPosition {m_currentScene->sceneCamera.getCameraPosition()};
		const auto up {m_currentScene->sceneCamera.getUp()};
		ubo.view = lookAt(
			cameraPosition,
			cameraPosition + m_currentScene->sceneCamera.getLookAtVector(),
			m_currentScene->sceneCamera.getUp()
		);
		ubo.proj = glm::perspective(
			glm::radians(45.0f),
			static_cast<float>(m_swapChainExtent.width) /
				static_cast<float>(m_swapChainExtent.height),
			0.1f,
			100.0f
		);
		ubo.proj[1][1] *= -1;

		vmaCopyMemoryToAllocation(
			m_allocator, &ubo, m_mvpAllocations[m_frameIndex][modelInstanceNum], 0, sizeof(ubo)
		);

		auto& commandBuffer {m_commandBuffers[m_frameIndex]};
		commandBuffer.bindVertexBuffers(0, *currMeshBuffers.vertexBuffer, {0});
		for (
			size_t primitiveIndex {}; primitiveIndex < currMeshBuffers.indexBuffers.size();
			++primitiveIndex
		) {
			commandBuffer.bindIndexBuffer(
				currMeshBuffers.indexBuffers[primitiveIndex], 0, vk::IndexType::eUint32
			);

			PushConstants matrixTextureIndices {
				modelInstanceNum, currMeshBuffers.textureIndices[primitiveIndex]
			};
			commandBuffer.pushConstants<PushConstants>(
				m_pipelineLayout,
				vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
				0,
				matrixTextureIndices
			);

			commandBuffer.drawIndexed(currMeshBuffers.indicesCount, 1, 0, 0, 0);
		}
	}
	for (auto& childIndex: modelInstance->childIndices) {
		++modelInstanceNum;
		queueDrawModelInstance(
			dynamic_cast<ModelInstance*>(m_currentScene->getNodes()[childIndex].get()),
			globalTransform * localTransform,
			modelInstanceNum
		);
	}
}
