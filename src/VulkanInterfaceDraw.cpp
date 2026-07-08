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
	auto [result, m_imageIndex] = m_swapChain.acquireNextImage(
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
		m_swapChainImages[m_imageIndex],
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
		.imageView = m_swapChainImageViews[m_imageIndex],
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
	for (const auto* parentNode: m_currentScene->getParentNodes()) {
		queueDrawModelInstance(*parentNode, glm::identity<glm::mat4>());
	}
	commandBuffer.endRendering();
	transition_image_layout(
		m_swapChainImages[m_imageIndex],
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
		.pSignalSemaphores = &*m_renderFinishedSemaphores[m_imageIndex]
	};
	m_queue.submit(submitInfo, *m_inFlightFences[m_frameIndex]);

	const vk::PresentInfoKHR presentInfoKHR {
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &*m_renderFinishedSemaphores[m_imageIndex],
		.swapchainCount = 1,
		.pSwapchains = &*m_swapChain,
		.pImageIndices = &m_imageIndex
	};
	result = m_queue.presentKHR(presentInfoKHR);

	assert(result == vk::Result::eSuccess);
	m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanInterface::queueDrawModelInstance(
	const ModelInstance& modelInstance, const glm::mat4& globalTransform
) {
	auto& commandBuffer {m_commandBuffers[m_frameIndex]};
	const auto& currMeshBuffers {*modelInstance.mesh};

	// update uniform buffer
	UniformBufferObject ubo {};
	glm::mat4 localTransform {
		glm::translate(modelInstance.position) * glm::toMat4(modelInstance.rotation) *
		glm::scale(modelInstance.scale)
	};
	ubo.model = globalTransform * localTransform;
	ubo.view = lookAt(
		glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
	);
	ubo.proj = glm::perspective(
		glm::radians(45.0f),
		static_cast<float>(m_swapChainExtent.width) / static_cast<float>(m_swapChainExtent.height),
		0.1f,
		10.0f
	);
	ubo.proj[1][1] *= -1;

	vmaCopyMemoryToAllocation(m_allocator, &ubo, m_mvpAllocations[m_frameIndex], 0, sizeof(ubo));

	commandBuffer.bindVertexBuffers(0, *currMeshBuffers.vertexBuffer, {0});
	commandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		m_pipelineLayout,
		0,
		*m_descriptorSets[m_frameIndex],
		nullptr
	);
	for (
		size_t primitiveIndex {}; primitiveIndex < currMeshBuffers.indexBuffers.size();
		++primitiveIndex
	) {
		commandBuffer.bindIndexBuffer(
			currMeshBuffers.indexBuffers[primitiveIndex], 0, vk::IndexType::eUint32
		);

		PushConstants textureIndex {currMeshBuffers.textureIndices[primitiveIndex]};
		commandBuffer.pushConstants<PushConstants>(
			m_pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, textureIndex
		);

		commandBuffer.drawIndexed(currMeshBuffers.indicesCount, 1, 0, 0, 0);
	}
	for (const auto& childIndex: modelInstance.childIndices) {
		queueDrawModelInstance(
			m_currentScene->getNodes()[childIndex], globalTransform * localTransform
		);
	}
}
