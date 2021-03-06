/*
 * VulkanPipelines.cpp
 *
 *  Created on: Sep 20, 2017
 *      Author: david
 */

#include "Rendering/Vulkan/VulkanPipelines.h"

#include <Rendering/Vulkan/VulkanRenderer.h>
#include <Rendering/Vulkan/VulkanEnums.h>

VulkanPipelines::VulkanPipelines (VulkanRenderer *parentVulkanRenderer)
{
	renderer = parentVulkanRenderer;
}

VulkanPipelines::~VulkanPipelines ()
{
	for (auto descriptorSetLayout : descriptorSetLayoutCache)
	{
		vkDestroyDescriptorSetLayout(renderer->device, descriptorSetLayout.second, nullptr);
	}
}

Pipeline VulkanPipelines::createGraphicsPipeline (const GraphicsPipelineInfo &pipelineInfo, RenderPass renderPass, uint32_t subpass)
{
	VulkanPipeline *vulkanPipeline = new VulkanPipeline();

	// Note that only the create infos that don't rely on pointers have been separated into other functions for clarity
	// Many of the vulkan structures do C-style arrays, and so we have to be careful about pointers

	std::vector<VkPipelineShaderStageCreateInfo> pipelineShaderStages = getPipelineShaderStages(pipelineInfo.stages);
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = getPipelineInputAssemblyInfo(pipelineInfo.inputAssemblyInfo);
	VkPipelineTessellationStateCreateInfo tessellationState = getPipelineTessellationInfo(pipelineInfo.tessellationInfo);
	VkPipelineRasterizationStateCreateInfo rastState = getPipelineRasterizationInfo(pipelineInfo.rasterizationInfo);
	VkPipelineMultisampleStateCreateInfo multisampleState = getPipelineMultisampleInfo(pipelineInfo.multisampleInfo);
	VkPipelineDepthStencilStateCreateInfo depthStencilState = getPipelineDepthStencilInfo(pipelineInfo.depthStencilInfo);

	// These are the create infos that rely on pointers in their data structs
	std::vector<VkVertexInputBindingDescription> inputBindings;
	std::vector<VkVertexInputAttributeDescription> inputAttribs;

	VkPipelineVertexInputStateCreateInfo vertexInputState = {};
	vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	{
		for (size_t i = 0; i < pipelineInfo.vertexInputInfo.vertexInputBindings.size(); i ++)
		{
			const VertexInputBinding &genericBinding = pipelineInfo.vertexInputInfo.vertexInputBindings[i];
			VkVertexInputBindingDescription binding = {};
			binding.binding = genericBinding.binding;
			binding.stride = genericBinding.stride;
			binding.inputRate = toVkVertexInputRate(genericBinding.inputRate);

			inputBindings.push_back(binding);
		}

		for (size_t i = 0; i < pipelineInfo.vertexInputInfo.vertexInputAttribs.size(); i ++)
		{
			const VertexInputAttrib &genericAttrib = pipelineInfo.vertexInputInfo.vertexInputAttribs[i];
			VkVertexInputAttributeDescription attrib = {};
			attrib.location = genericAttrib.location;
			attrib.binding = genericAttrib.binding;
			attrib.format = toVkFormat(genericAttrib.format);
			attrib.offset = genericAttrib.offset;

			inputAttribs.push_back(attrib);
		}

		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(inputBindings.size());
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(inputAttribs.size());
		vertexInputState.pVertexBindingDescriptions = inputBindings.data();
		vertexInputState.pVertexAttributeDescriptions = inputAttribs.data();
	}

	std::vector<VkViewport> viewports;
	std::vector<VkRect2D> scissors;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

	{
		for (size_t i = 0; i < pipelineInfo.viewportInfo.viewports.size(); i ++)
		{
			const Viewport &genericViewport = pipelineInfo.viewportInfo.viewports[i];
			VkViewport viewport = {};
			viewport.x = genericViewport.x;
			viewport.y = genericViewport.y;
			viewport.width = genericViewport.width;
			viewport.height = genericViewport.height;
			viewport.minDepth = genericViewport.minDepth;
			viewport.maxDepth = genericViewport.maxDepth;

			viewports.push_back(viewport);
		}

		for (size_t i = 0; i < pipelineInfo.viewportInfo.scissors.size(); i ++)
		{
			const Scissor &genericScissor = pipelineInfo.viewportInfo.scissors[i];
			VkRect2D scissor = {};
			scissor.offset = {genericScissor.x, genericScissor.y};
			scissor.extent = { genericScissor.width, genericScissor.height };

			scissors.push_back(scissor);
		}

		viewportState.viewportCount = static_cast<uint32_t>(viewports.size());
		viewportState.scissorCount = static_cast<uint32_t>(scissors.size());
		viewportState.pViewports = viewports.data();
		viewportState.pScissors = scissors.data();
	}

	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;

	VkPipelineColorBlendStateCreateInfo colorBlendState = {};
	colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	{
		colorBlendState.logicOpEnable = static_cast<VkBool32>(pipelineInfo.colorBlendInfo.logicOpEnable);
		colorBlendState.logicOp = toVkLogicOp(pipelineInfo.colorBlendInfo.logicOp);
		colorBlendState.blendConstants[0] = pipelineInfo.colorBlendInfo.blendConstants[0];
		colorBlendState.blendConstants[1] = pipelineInfo.colorBlendInfo.blendConstants[1];
		colorBlendState.blendConstants[2] = pipelineInfo.colorBlendInfo.blendConstants[2];
		colorBlendState.blendConstants[3] = pipelineInfo.colorBlendInfo.blendConstants[3];

		for (size_t i = 0; i < pipelineInfo.colorBlendInfo.attachments.size(); i ++)
		{
			const PipelineColorBlendAttachment &genericAttachment = pipelineInfo.colorBlendInfo.attachments[i];
			VkPipelineColorBlendAttachmentState attachment = {};
			attachment.blendEnable = static_cast<VkBool32>(genericAttachment.blendEnable);
			attachment.srcColorBlendFactor = toVkBlendFactor(genericAttachment.srcColorBlendFactor);
			attachment.dstColorBlendFactor = toVkBlendFactor(genericAttachment.dstColorBlendFactor);
			attachment.colorBlendOp = toVkBlendOp(genericAttachment.colorBlendOp);
			attachment.srcAlphaBlendFactor = toVkBlendFactor(genericAttachment.srcAlphaBlendFactor);
			attachment.dstAlphaBlendFactor = toVkBlendFactor(genericAttachment.dstAlphaBlendFactor);
			attachment.alphaBlendOp = toVkBlendOp(genericAttachment.alphaBlendOp);
			attachment.colorWriteMask = static_cast<VkColorComponentFlags>(genericAttachment.colorWriteMask);

			colorBlendAttachments.push_back(attachment);
		}

		colorBlendState.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		colorBlendState.pAttachments = colorBlendAttachments.data();
	}

	std::vector<VkDynamicState> dynamicStates;

	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

	{
		for (size_t i = 0; i < pipelineInfo.dynamicStateInfo.dynamicStates.size(); i ++)
		{
			dynamicStates.push_back(toVkDynamicState(pipelineInfo.dynamicStateInfo.dynamicStates[i]));
		}

		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();
	}

	VkPipelineRasterizationStateRasterizationOrderAMD rastOrderAMD = {};

	if (VulkanExtensions::enabled_VK_AMD_rasterization_order)
	{
		rastOrderAMD.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD;
		rastOrderAMD.rasterizationOrder = pipelineInfo.rasterizationInfo.enableOutOfOrderRasterization ? VK_RASTERIZATION_ORDER_RELAXED_AMD : VK_RASTERIZATION_ORDER_STRICT_AMD;
		rastState.pNext = &rastOrderAMD;
	}

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.stageCount = static_cast<uint32_t>(pipelineShaderStages.size());
	pipelineCreateInfo.pStages = pipelineShaderStages.data();
	pipelineCreateInfo.pVertexInputState = &vertexInputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pTessellationState = &tessellationState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pRasterizationState = &rastState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pDynamicState = dynamicStates.size() == 0 ? nullptr : &dynamicState;
	pipelineCreateInfo.renderPass = static_cast<VulkanRenderPass*>(renderPass)->renderPassHandle;
	pipelineCreateInfo.subpass = subpass;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCreateInfo.basePipelineIndex = -1;

	std::vector<VkPushConstantRange> vulkanPushConstantRanges;
	std::vector<VkDescriptorSetLayout> vulkanDescSetLayouts;

	for (size_t i = 0; i < pipelineInfo.inputPushConstantRanges.size(); i ++)
	{
		const PushConstantRange &genericPushRange = pipelineInfo.inputPushConstantRanges[i];
		VkPushConstantRange vulkanPushRange = {};
		vulkanPushRange.stageFlags = genericPushRange.stageFlags;
		vulkanPushRange.size = genericPushRange.size;
		vulkanPushRange.offset = genericPushRange.offset;

		vulkanPushConstantRanges.push_back(vulkanPushRange);
	}

	for (size_t i = 0; i < pipelineInfo.inputSetLayouts.size(); i ++)
	{
		vulkanDescSetLayouts.push_back(createDescriptorSetLayout(pipelineInfo.inputSetLayouts[i]));
	}

	VkPipelineLayoutCreateInfo layoutCreateInfo = {};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pushConstantRangeCount = static_cast<uint32_t>(pipelineInfo.inputPushConstantRanges.size());
	layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(pipelineInfo.inputSetLayouts.size());
	layoutCreateInfo.pPushConstantRanges = vulkanPushConstantRanges.data();
	layoutCreateInfo.pSetLayouts = vulkanDescSetLayouts.data();

	VK_CHECK_RESULT(vkCreatePipelineLayout(renderer->device, &layoutCreateInfo, nullptr, &vulkanPipeline->pipelineLayoutHandle));

	pipelineCreateInfo.layout = vulkanPipeline->pipelineLayoutHandle;

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &vulkanPipeline->pipelineHandle));

	return vulkanPipeline;
}

Pipeline VulkanPipelines::createComputePipeline(const ComputePipelineInfo &pipelineInfo)
{
	VulkanPipeline *vulkanPipeline = new VulkanPipeline();

	VkComputePipelineCreateInfo pipelineCreateInfo = {};
	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCreateInfo.basePipelineIndex = -1;
	pipelineCreateInfo.flags = 0;

	VkPipelineShaderStageCreateInfo computeShaderStage = {};
	computeShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	computeShaderStage.flags = 0;
	computeShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computeShaderStage.module = static_cast<VulkanShaderModule*>(pipelineInfo.shader.module)->module;
	computeShaderStage.pName = pipelineInfo.shader.entry;
	computeShaderStage.pSpecializationInfo = nullptr;

	pipelineCreateInfo.stage = computeShaderStage;

	std::vector<VkPushConstantRange> vulkanPushConstantRanges;
	std::vector<VkDescriptorSetLayout> vulkanDescSetLayouts;

	for (size_t i = 0; i < pipelineInfo.inputPushConstantRanges.size(); i++)
	{
		const PushConstantRange &genericPushRange = pipelineInfo.inputPushConstantRanges[i];
		VkPushConstantRange vulkanPushRange = {};
		vulkanPushRange.stageFlags = genericPushRange.stageFlags;
		vulkanPushRange.size = genericPushRange.size;
		vulkanPushRange.offset = genericPushRange.offset;

		vulkanPushConstantRanges.push_back(vulkanPushRange);
	}

	for (size_t i = 0; i < pipelineInfo.inputSetLayouts.size(); i++)
	{
		vulkanDescSetLayouts.push_back(createDescriptorSetLayout(pipelineInfo.inputSetLayouts[i]));
	}

	VkPipelineLayoutCreateInfo layoutCreateInfo = {};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCreateInfo.pushConstantRangeCount = static_cast<uint32_t>(pipelineInfo.inputPushConstantRanges.size());
	layoutCreateInfo.setLayoutCount = static_cast<uint32_t>(pipelineInfo.inputSetLayouts.size());
	layoutCreateInfo.pPushConstantRanges = vulkanPushConstantRanges.data();
	layoutCreateInfo.pSetLayouts = vulkanDescSetLayouts.data();

	VK_CHECK_RESULT(vkCreatePipelineLayout(renderer->device, &layoutCreateInfo, nullptr, &vulkanPipeline->pipelineLayoutHandle));

	pipelineCreateInfo.layout = vulkanPipeline->pipelineLayoutHandle;

	VK_CHECK_RESULT(vkCreateComputePipelines(renderer->device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &vulkanPipeline->pipelineHandle));

	return vulkanPipeline;
}

VkDescriptorSetLayout VulkanPipelines::createDescriptorSetLayout (const std::vector<DescriptorSetLayoutBinding> &layoutBindings)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	for (size_t i = 0; i < layoutBindings.size(); i ++)
	{
		const DescriptorSetLayoutBinding &genericBinding = layoutBindings[i];
		VkDescriptorSetLayoutBinding vulkanBinding = {};
		vulkanBinding.stageFlags = genericBinding.stageFlags;
		vulkanBinding.binding = genericBinding.binding;
		vulkanBinding.descriptorCount = genericBinding.descriptorCount;
		vulkanBinding.descriptorType = toVkDescriptorType(genericBinding.descriptorType);

		bindings.push_back(vulkanBinding);
	}

	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo = {};
	setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setLayoutCreateInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	setLayoutCreateInfo.pBindings = bindings.data();

	return createDescriptorSetLayout(setLayoutCreateInfo);
}

inline bool compareDescSetLayoutCacheInfos(const VulkanDescriptorSetLayoutCacheInfo &c0, const VulkanDescriptorSetLayoutCacheInfo &c1)
{
	// Check the obvious first
	if (c0.bindings.size() != c1.bindings.size() || c0.flags != c1.flags)
		return false;

	// Check to make sure each binding is the same
	for (size_t i = 0; i < c0.bindings.size(); i++)
	{
		const VkDescriptorSetLayoutBinding& bind0 = c0.bindings[i];
		const VkDescriptorSetLayoutBinding& bind1 = c1.bindings[i];

		if ((bind0.binding != bind1.binding) || (bind0.descriptorCount != bind1.descriptorCount) || (bind0.descriptorType != bind1.descriptorType) || (bind0.stageFlags != bind1.stageFlags))
			return false;
	}

	return true;
}

/*
 * Attempts to reuse a descriptor set layout object from the cache, but will make a new one if needed.
 */
VkDescriptorSetLayout VulkanPipelines::createDescriptorSetLayout (const VkDescriptorSetLayoutCreateInfo &setLayoutInfo)
{
	VulkanDescriptorSetLayoutCacheInfo cacheInfo = {};
	cacheInfo.flags = setLayoutInfo.flags;
	cacheInfo.bindings = std::vector<VkDescriptorSetLayoutBinding>(setLayoutInfo.pBindings, setLayoutInfo.pBindings + setLayoutInfo.bindingCount);

	// Try and find a matching cached set layout to use if possible
	for (size_t i = 0; i < descriptorSetLayoutCache.size(); i++)
	{
		const VulkanDescriptorSetLayoutCacheInfo &candCacheInfo = descriptorSetLayoutCache[i].first;

		if (compareDescSetLayoutCacheInfos(cacheInfo, candCacheInfo))
			return descriptorSetLayoutCache[i].second;
	}

	// If there's no matchign cached set layout, make a new one and add it

	VkDescriptorSetLayout setLayout;

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(renderer->device, &setLayoutInfo, nullptr, &setLayout));

	descriptorSetLayoutCache.push_back(std::make_pair(cacheInfo, setLayout));

	return setLayout;
}

std::vector<VkPipelineShaderStageCreateInfo> VulkanPipelines::getPipelineShaderStages (const std::vector<PipelineShaderStage> &stages)
{
	std::vector<VkPipelineShaderStageCreateInfo> vulkanStageInfo;

	for (size_t i = 0; i < stages.size(); i ++)
	{
		const VulkanShaderModule *shaderModule = static_cast<VulkanShaderModule*>(stages[i].module);
		VkPipelineShaderStageCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		createInfo.pName = stages[i].entry;
		createInfo.module = shaderModule->module;
		createInfo.stage = toVkShaderStageFlagBits(shaderModule->stage);

		vulkanStageInfo.push_back(createInfo);
	}

	return vulkanStageInfo;
}

VkPipelineInputAssemblyStateCreateInfo VulkanPipelines::getPipelineInputAssemblyInfo (const PipelineInputAssemblyInfo &info)
{
	VkPipelineInputAssemblyStateCreateInfo inputCreateInfo = {};
	inputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputCreateInfo.primitiveRestartEnable = static_cast<VkBool32>(info.primitiveRestart);
	inputCreateInfo.topology = toVkPrimitiveTopology(info.topology);

	return inputCreateInfo;
}

VkPipelineTessellationStateCreateInfo VulkanPipelines::getPipelineTessellationInfo (const PipelineTessellationInfo &info)
{
	VkPipelineTessellationStateCreateInfo tessellationCreateInfo = {};
	tessellationCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
	tessellationCreateInfo.patchControlPoints = info.patchControlPoints;

	return tessellationCreateInfo;
}

VkPipelineRasterizationStateCreateInfo VulkanPipelines::getPipelineRasterizationInfo (const PipelineRasterizationInfo &info)
{
	VkPipelineRasterizationStateCreateInfo rastCreateInfo = {};
	rastCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rastCreateInfo.depthClampEnable = static_cast<VkBool32>(info.depthClampEnable);
	rastCreateInfo.rasterizerDiscardEnable = static_cast<VkBool32>(info.rasterizerDiscardEnable);
	rastCreateInfo.polygonMode = toVkPolygonMode(info.polygonMode);
	rastCreateInfo.cullMode = static_cast<VkCullModeFlags>(info.cullMode);
	rastCreateInfo.frontFace = info.clockwiseFrontFace ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
	// The depth bias info will go here when (if) I implement it
	rastCreateInfo.lineWidth = info.lineWidth;

	return rastCreateInfo;
}

VkPipelineMultisampleStateCreateInfo VulkanPipelines::getPipelineMultisampleInfo (const PipelineMultisampleInfo &info)
{
	VkPipelineMultisampleStateCreateInfo msCreateInfo = {};
	msCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	msCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Later if I add msaa, the rest will go here

	return msCreateInfo;
}

VkPipelineDepthStencilStateCreateInfo VulkanPipelines::getPipelineDepthStencilInfo (const PipelineDepthStencilInfo &info)
{
	VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo = {};
	depthStencilCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilCreateInfo.depthTestEnable = static_cast<VkBool32>(info.enableDepthTest);
	depthStencilCreateInfo.depthWriteEnable = static_cast<VkBool32>(info.enableDepthWrite);
	depthStencilCreateInfo.depthCompareOp = toVkCompareOp(info.depthCompareOp);
	depthStencilCreateInfo.depthBoundsTestEnable = static_cast<VkBool32>(info.depthBoundsTestEnable);
	// The stencil stuff will go here when (if) I implement it
	depthStencilCreateInfo.minDepthBounds = info.minDepthBounds;
	depthStencilCreateInfo.maxDepthBounds = info.maxDepthBounds;

	return depthStencilCreateInfo;
}

