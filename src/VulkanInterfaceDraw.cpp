#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/transform.hpp>

#include "Scene.hpp"
#include "VulkanInterface.hpp"

void VulkanInterface::drawFrame() {
	bool cameraMoved {m_currentScene->handleCameraMovement()};
	updateBuffers(cameraMoved);
	const bool ddgiOperations {nextFrame(DDGI_FRAMERATE, m_ddgiLastFrame)};
	int secondaryDdgiOperationIndex {3};
	if (ddgiOperations) {
		if (m_ddgiFrameIndex % 2 == 0) {
			secondaryDdgiOperationIndex = 1;
		} else if (m_ddgiFrameIndex % 4 == 3) {
			secondaryDdgiOperationIndex = 2;
		} else if (m_ddgiFrameIndex % 8 == 1) {
			secondaryDdgiOperationIndex = 3;
		}
		updateDdgi(0, secondaryDdgiOperationIndex);
	}
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
	updateTlas();

	m_device.resetFences(*m_inFlightFences[m_frameIndex]); // check fences

	m_currentScene->propagateUpdates();

	auto& commandBuffer {m_commandBuffers[m_frameIndex]}; // per-draw commands

	commandBuffer.reset();
	commandBuffer.begin({});
	// Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
	transitionImageLayoutPipeline(
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
	transitionImageLayoutPipeline(
		m_depthImage,
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
	if (ddgiOperations) {
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eCompute,
			m_computeRaySamplePipelineLayout,
			0,
			*m_descriptorSets[m_frameIndex],
			nullptr
		);

		commandBuffer.bindPipeline(
			vk::PipelineBindPoint::eCompute, m_computeRaySamplePipeline
		); // compute
		uint32_t cascadeIndex {0};
		commandBuffer.pushConstants(
			m_computeRaySamplePipelineLayout,
			vk::ShaderStageFlagBits::eCompute,
			0,
			sizeof(uint32_t),
			&cascadeIndex
		);
		commandBuffer.dispatch(m_ddgiCascades[0].probeCount, 1, 1);
		if (secondaryDdgiOperationIndex != -1) {
			commandBuffer.pushConstants(
				m_computeRaySamplePipelineLayout,
				vk::ShaderStageFlagBits::eCompute,
				0,
				sizeof(uint32_t),
				&secondaryDdgiOperationIndex
			);
			commandBuffer.dispatch(m_ddgiCascades[secondaryDdgiOperationIndex].probeCount, 1, 1);
		}
		m_pipelineMemoryBarrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
			.srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead
		};
		m_pipelineDependencyInfo = {
			.memoryBarrierCount = 1, .pMemoryBarriers = &m_pipelineMemoryBarrier
		};
		commandBuffer.pipelineBarrier2(m_pipelineDependencyInfo);

		commandBuffer.bindPipeline(
			vk::PipelineBindPoint::eCompute, m_computeProbeIrradianceClearPipeline
		);
		commandBuffer.dispatchIndirect(m_ddgiClearDispatchCommandBuffers[m_frameIndex], 0);
		commandBuffer.bindPipeline(
			vk::PipelineBindPoint::eCompute, m_computeProbeDepthClearPipeline
		);
		commandBuffer.dispatchIndirect(m_ddgiClearDispatchCommandBuffers[m_frameIndex], 0);
		m_pipelineMemoryBarrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader |
							vk::PipelineStageFlagBits2::eFragmentShader,
			.srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead
		};
		m_pipelineDependencyInfo = {
			.memoryBarrierCount = 1, .pMemoryBarriers = &m_pipelineMemoryBarrier
		};
		commandBuffer.pipelineBarrier2(m_pipelineDependencyInfo);

		commandBuffer.bindPipeline(
			vk::PipelineBindPoint::eCompute, m_computeProbeIrradianceUpdatePipeline
		);
		commandBuffer.pushConstants(
			m_computeProbeIrradianceUpdatePipelineLayout,
			vk::ShaderStageFlagBits::eCompute,
			0,
			sizeof(uint32_t),
			&cascadeIndex
		);
		commandBuffer.dispatch(m_ddgiCascades[0].probeCount, 1, 1);
		if (secondaryDdgiOperationIndex != -1) {
			commandBuffer.pushConstants(
				m_computeProbeIrradianceUpdatePipelineLayout,
				vk::ShaderStageFlagBits::eCompute,
				0,
				sizeof(uint32_t),
				&secondaryDdgiOperationIndex
			);
			commandBuffer.dispatch(m_ddgiCascades[secondaryDdgiOperationIndex].probeCount, 1, 1);
		}

		commandBuffer.bindPipeline(
			vk::PipelineBindPoint::eCompute, m_computeProbeDepthUpdatePipeline
		);
		commandBuffer.pushConstants(
			m_computeProbeDepthUpdatePipelineLayout,
			vk::ShaderStageFlagBits::eCompute,
			0,
			sizeof(uint32_t),
			&cascadeIndex
		);
		commandBuffer.dispatch(m_ddgiCascades[0].probeCount, 1, 1);
		if (secondaryDdgiOperationIndex != -1) {
			commandBuffer.pushConstants(
				m_computeProbeDepthUpdatePipelineLayout,
				vk::ShaderStageFlagBits::eCompute,
				0,
				sizeof(uint32_t),
				&secondaryDdgiOperationIndex
			);
			commandBuffer.dispatch(m_ddgiCascades[secondaryDdgiOperationIndex].probeCount, 1, 1);
		}

		m_pipelineMemoryBarrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
			.srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
			.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader |
							vk::PipelineStageFlagBits2::eComputeShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead
		};
		m_pipelineDependencyInfo = {
			.memoryBarrierCount = 1, .pMemoryBarriers = &m_pipelineMemoryBarrier
		};
		commandBuffer.pipelineBarrier2(m_pipelineDependencyInfo);

		m_ddgiFrameIndex = (m_ddgiFrameIndex + 1) % DDGI_FRAMERATE;
	}

	commandBuffer.beginRendering(renderingInfo);
	commandBuffer.bindDescriptorSets(
		vk::PipelineBindPoint::eGraphics,
		m_graphicsPipelineLayout,
		0,
		*m_descriptorSets[m_frameIndex],
		nullptr
	);
	commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline); // graphics
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
	commandBuffer.bindVertexBuffers(0, m_vertexBuffer, {0});
	commandBuffer.bindIndexBuffer(m_indexBuffer, 0, vk::IndexType::eUint32);

	commandBuffer.drawIndexedIndirect(
		m_drawCommandsBuffers[m_frameIndex],
		0,
		m_opaqueDrawCallsCount + m_transparentDrawCallsCount + (m_drawDdgiProbes ? 1 : 0),
		sizeof(vk::DrawIndexedIndirectCommand)
	);

	commandBuffer.endRendering();
	transitionImageLayoutPipeline(
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
