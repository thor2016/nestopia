#include "video/ViewerVulkan.h"

#include <cstring>
#include <vector>

#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QWidget>
#include <QtDebug>

#include "core/NesCore.h"
#include "video/VulkanShaders.h"

namespace
{
	constexpr VkFormat kTextureFormat = VK_FORMAT_B8G8R8A8_UNORM;
	constexpr uint32_t kTextureWidth = NesCore::kMaxFrameWidth;
	constexpr uint32_t kTextureHeight = NesCore::kFrameHeight;
	constexpr VkDeviceSize kTextureBytes = VkDeviceSize(kTextureWidth) * kTextureHeight * 4;

	struct PushConstants
	{
		float rect[4];
		float uvScale[2];
	};
}

class VulkanRenderer : public QVulkanWindowRenderer
{
	public:
		VulkanRenderer(QVulkanWindow *vulkanWindow, ViewerVulkan *owner)
			: window(vulkanWindow), viewer(owner) {}

		void initResources(void) override;
		void releaseResources(void) override;
		void startNextFrame(void) override;

	private:
		struct Staging
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			void *mapped = nullptr;
		};

		uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted) const;
		bool createTexture(void);
		bool createStaging(void);
		bool createPipeline(void);
		VkShaderModule createShader(const uint32_t *code, size_t bytes) const;
		void transition(VkCommandBuffer cb, VkImageLayout from, VkImageLayout to,
			VkAccessFlags srcAccess, VkAccessFlags dstAccess,
			VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

		QVulkanWindow *window;
		ViewerVulkan *viewer;
		QVulkanDeviceFunctions *functions = nullptr;
		VkDevice device = VK_NULL_HANDLE;

		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory imageMemory = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VkSampler samplers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
		VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
		VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		std::vector<Staging> staging;
		bool imageReady = false;
};

uint32_t VulkanRenderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted) const
{
	VkPhysicalDeviceMemoryProperties properties;

	window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(window->physicalDevice(), &properties);

	for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
	{
		if ((typeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted)
		{
			return i;
		}
	}

	return UINT32_MAX;
}

VkShaderModule VulkanRenderer::createShader(const uint32_t *code, size_t bytes) const
{
	VkShaderModuleCreateInfo info = {};
	VkShaderModule module = VK_NULL_HANDLE;

	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = bytes;
	info.pCode = code;

	if (functions->vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
	{
		return VK_NULL_HANDLE;
	}

	return module;
}

bool VulkanRenderer::createTexture(void)
{
	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = kTextureFormat;
	imageInfo.extent = {kTextureWidth, kTextureHeight, 1};
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (functions->vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
	{
		return false;
	}

	VkMemoryRequirements requirements;
	functions->vkGetImageMemoryRequirements(device, image, &requirements);

	uint32_t type = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (type == UINT32_MAX)
	{
		type = findMemoryType(requirements.memoryTypeBits, 0);
	}

	VkMemoryAllocateInfo allocate = {};
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = type;

	if (type == UINT32_MAX ||
		functions->vkAllocateMemory(device, &allocate, nullptr, &imageMemory) != VK_SUCCESS ||
		functions->vkBindImageMemory(device, image, imageMemory, 0) != VK_SUCCESS)
	{
		return false;
	}

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = kTextureFormat;
	viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	return functions->vkCreateImageView(device, &viewInfo, nullptr, &imageView) == VK_SUCCESS;
}

bool VulkanRenderer::createStaging(void)
{
	// One buffer per frame in flight, the GPU may still read the previous one
	staging.resize(size_t(window->concurrentFrameCount()));

	for (Staging &entry : staging)
	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = kTextureBytes;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (functions->vkCreateBuffer(device, &bufferInfo, nullptr, &entry.buffer) != VK_SUCCESS)
		{
			return false;
		}

		VkMemoryRequirements requirements;
		functions->vkGetBufferMemoryRequirements(device, entry.buffer, &requirements);

		const uint32_t type = findMemoryType(requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VkMemoryAllocateInfo allocate = {};
		allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocate.allocationSize = requirements.size;
		allocate.memoryTypeIndex = type;

		if (type == UINT32_MAX ||
			functions->vkAllocateMemory(device, &allocate, nullptr, &entry.memory) != VK_SUCCESS ||
			functions->vkBindBufferMemory(device, entry.buffer, entry.memory, 0) != VK_SUCCESS ||
			functions->vkMapMemory(device, entry.memory, 0, VK_WHOLE_SIZE, 0, &entry.mapped) != VK_SUCCESS)
		{
			return false;
		}
	}

	return true;
}

bool VulkanRenderer::createPipeline(void)
{
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;

	if (functions->vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
	{
		return false;
	}

	VkPushConstantRange pushRange = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &setLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushRange;

	if (functions->vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
	{
		return false;
	}

	// Nearest and linear sampling, picked per draw
	for (int i = 0; i < 2; ++i)
	{
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = i == 0 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
		samplerInfo.minFilter = samplerInfo.magFilter;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		if (functions->vkCreateSampler(device, &samplerInfo, nullptr, &samplers[i]) != VK_SUCCESS)
		{
			return false;
		}
	}

	VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = 2;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;

	if (functions->vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
	{
		return false;
	}

	VkDescriptorSetLayout layouts[2] = {setLayout, setLayout};

	VkDescriptorSetAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = descriptorPool;
	allocateInfo.descriptorSetCount = 2;
	allocateInfo.pSetLayouts = layouts;

	if (functions->vkAllocateDescriptorSets(device, &allocateInfo, descriptorSets) != VK_SUCCESS)
	{
		return false;
	}

	for (int i = 0; i < 2; ++i)
	{
		VkDescriptorImageInfo imageInfo = {samplers[i], imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

		VkWriteDescriptorSet write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = descriptorSets[i];
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		functions->vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	VkShaderModule vertex = createShader(kBlitVertSpv, sizeof(kBlitVertSpv));
	VkShaderModule fragment = createShader(kBlitFragSpv, sizeof(kBlitFragSpv));

	if (vertex == VK_NULL_HANDLE || fragment == VK_NULL_HANDLE)
	{
		if (vertex != VK_NULL_HANDLE) functions->vkDestroyShaderModule(device, vertex, nullptr);
		if (fragment != VK_NULL_HANDLE) functions->vkDestroyShaderModule(device, fragment, nullptr);
		return false;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertex;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragment;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkPipelineViewportStateCreateInfo viewport = {};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterization = {};
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = window->sampleCountFlagBits();

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	VkPipelineColorBlendAttachmentState blendAttachment = {};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend = {};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAttachment;

	const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewport;
	pipelineInfo.pRasterizationState = &rasterization;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &blend;
	pipelineInfo.pDynamicState = &dynamic;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = window->defaultRenderPass();
	pipelineInfo.subpass = 0;

	const VkResult result = functions->vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

	functions->vkDestroyShaderModule(device, vertex, nullptr);
	functions->vkDestroyShaderModule(device, fragment, nullptr);

	return result == VK_SUCCESS;
}

void VulkanRenderer::initResources(void)
{
	device = window->device();
	functions = window->vulkanInstance()->deviceFunctions(device);
	imageReady = false;

	if (!createTexture() || !createStaging() || !createPipeline())
	{
		qWarning() << "Vulkan viewer: resource creation failed";
		releaseResources();
	}
}

void VulkanRenderer::releaseResources(void)
{
	if (functions == nullptr)
	{
		return;
	}

	functions->vkDeviceWaitIdle(device);

	if (pipeline != VK_NULL_HANDLE) functions->vkDestroyPipeline(device, pipeline, nullptr);
	if (descriptorPool != VK_NULL_HANDLE) functions->vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	if (pipelineLayout != VK_NULL_HANDLE) functions->vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	if (setLayout != VK_NULL_HANDLE) functions->vkDestroyDescriptorSetLayout(device, setLayout, nullptr);

	for (VkSampler &sampler : samplers)
	{
		if (sampler != VK_NULL_HANDLE) functions->vkDestroySampler(device, sampler, nullptr);
		sampler = VK_NULL_HANDLE;
	}

	for (Staging &entry : staging)
	{
		if (entry.mapped != nullptr) functions->vkUnmapMemory(device, entry.memory);
		if (entry.buffer != VK_NULL_HANDLE) functions->vkDestroyBuffer(device, entry.buffer, nullptr);
		if (entry.memory != VK_NULL_HANDLE) functions->vkFreeMemory(device, entry.memory, nullptr);
	}

	if (imageView != VK_NULL_HANDLE) functions->vkDestroyImageView(device, imageView, nullptr);
	if (image != VK_NULL_HANDLE) functions->vkDestroyImage(device, image, nullptr);
	if (imageMemory != VK_NULL_HANDLE) functions->vkFreeMemory(device, imageMemory, nullptr);

	staging.clear();
	pipeline = VK_NULL_HANDLE;
	descriptorPool = VK_NULL_HANDLE;
	descriptorSets[0] = descriptorSets[1] = VK_NULL_HANDLE;
	pipelineLayout = VK_NULL_HANDLE;
	setLayout = VK_NULL_HANDLE;
	imageView = VK_NULL_HANDLE;
	image = VK_NULL_HANDLE;
	imageMemory = VK_NULL_HANDLE;
	imageReady = false;
	functions = nullptr;
}

void VulkanRenderer::transition(VkCommandBuffer cb, VkImageLayout from, VkImageLayout to,
	VkAccessFlags srcAccess, VkAccessFlags dstAccess,
	VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = from;
	barrier.newLayout = to;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	functions->vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanRenderer::startNextFrame(void)
{
	const QSize size = window->swapChainImageSize();
	VkCommandBuffer cb = window->currentCommandBuffer();
	const QImage &frame = viewer->frame();

	if (functions != nullptr && pipeline != VK_NULL_HANDLE && !frame.isNull() && viewer->frameChanged())
	{
		Staging &entry = staging[size_t(window->currentFrame())];
		const size_t bytes = size_t(frame.width()) * size_t(frame.height()) * 4;

		std::memcpy(entry.mapped, frame.constBits(), bytes);

		transition(cb,
			imageReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			imageReady ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
			imageReady ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT);

		VkBufferImageCopy region = {};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {uint32_t(frame.width()), uint32_t(frame.height()), 1};

		functions->vkCmdCopyBufferToImage(cb, entry.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		transition(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		imageReady = true;
		viewer->clearFrameChanged();
	}

	VkClearValue clearValues[2] = {};
	clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
	clearValues[1].depthStencil = {1.0f, 0};

	VkRenderPassBeginInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	passInfo.renderPass = window->defaultRenderPass();
	passInfo.framebuffer = window->currentFramebuffer();
	passInfo.renderArea.extent = {uint32_t(size.width()), uint32_t(size.height())};
	passInfo.clearValueCount = 2;
	passInfo.pClearValues = clearValues;

	functions->vkCmdBeginRenderPass(cb, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

	const QRect target = viewer->targetRect(size);

	if (imageReady && !target.isEmpty() && pipeline != VK_NULL_HANDLE)
	{
		VkViewport viewport = {0.0f, 0.0f, float(size.width()), float(size.height()), 0.0f, 1.0f};
		VkRect2D scissor = {{0, 0}, {uint32_t(size.width()), uint32_t(size.height())}};
		PushConstants push;

		push.rect[0] = 2.0f * target.left() / size.width() - 1.0f;
		push.rect[1] = 2.0f * target.top() / size.height() - 1.0f;
		push.rect[2] = 2.0f * (target.right() + 1) / size.width() - 1.0f;
		push.rect[3] = 2.0f * (target.bottom() + 1) / size.height() - 1.0f;
		push.uvScale[0] = float(frame.width()) / float(kTextureWidth);
		push.uvScale[1] = float(frame.height()) / float(kTextureHeight);

		functions->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		functions->vkCmdSetViewport(cb, 0, 1, &viewport);
		functions->vkCmdSetScissor(cb, 0, 1, &scissor);
		functions->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
			&descriptorSets[viewer->linearFilter() ? 1 : 0], 0, nullptr);
		functions->vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		functions->vkCmdDraw(cb, 4, 1, 0, 0);
	}

	functions->vkCmdEndRenderPass(cb);

	window->frameReady();
}

VulkanWindow::VulkanWindow(ViewerVulkan *owner)
	: viewer(owner)
{
}

QVulkanWindowRenderer *VulkanWindow::createRenderer(void)
{
	return new VulkanRenderer(this, viewer);
}

ViewerVulkan::ViewerVulkan(const Options &options, QWidget *parent)
	: ViewerBase(options),
	  parentWidget(parent),
	  window(nullptr),
	  container(nullptr)
{
}

ViewerVulkan::~ViewerVulkan(void)
{
	// Destroys the Vulkan window as well, before the instance goes away
	delete container;
}

bool ViewerVulkan::init(void)
{
	if (!instance.create())
	{
		qWarning() << "Vulkan viewer: instance creation failed";
		return false;
	}

	uint32_t deviceCount = 0;

	instance.functions()->vkEnumeratePhysicalDevices(instance.vkInstance(), &deviceCount, nullptr);

	if (deviceCount == 0)
	{
		qWarning() << "Vulkan viewer: no Vulkan device found";
		return false;
	}

	window = new VulkanWindow(this);
	window->setVulkanInstance(&instance);
	// The frame is already in display values, an sRGB swap chain would brighten it
	window->setPreferredColorFormats({VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM});

	container = QWidget::createWindowContainer(window, parentWidget);
	container->setMinimumSize(256, 240);
	container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	return true;
}

QString ViewerVulkan::driverInfo(void) const
{
	if (window != nullptr && window->isValid() && window->physicalDeviceProperties() != nullptr)
	{
		return QStringLiteral("Vulkan (%1)").arg(QString::fromUtf8(window->physicalDeviceProperties()->deviceName));
	}

	return QStringLiteral("Vulkan");
}

void ViewerVulkan::queueRedraw(void)
{
	if (window != nullptr)
	{
		window->requestUpdate();
	}
}
