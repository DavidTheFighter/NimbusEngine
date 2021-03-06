/*
 * VulkanSwapchain.h
 *
 *  Created on: Sep 13, 2017
 *      Author: david
 */

#ifndef RENDERING_VULKAN_VULKANSWAPCHAIN_H_
#define RENDERING_VULKAN_VULKANSWAPCHAIN_H_

#include <common.h>
#include <Rendering/Vulkan/vulkan_common.h>

class Window;

typedef struct SwapchainSupportDetails
{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
} SwapchainSupportDetails;

typedef struct WindowSwapchain
{
		Window *window;
		VkSwapchainKHR swapchain;
		VkSurfaceKHR surface;
		VkExtent2D swapchainExtent;
		SwapchainSupportDetails swapchainDetails;
		VkFormat swapchainImageFormat;

		VkSemaphore swapchainNextImageAvailableSemaphore;
		VkSemaphore swapchainDoneRenderingSemaphore;

		std::vector<VkImage> swapchainImages;
		std::vector<VkImageView> swapchainImageViews;
		std::vector<VkFramebuffer> swapchainFramebuffers;
		std::vector<VkCommandBuffer> swapchainCommandBuffers;

		VkDescriptorPool swapchainDescriptorPool;
		VkDescriptorSet swapchainDescriptorSet;

		VkRenderPass swapchainRenderPass;
		VkPipeline swapchainPipeline;
} WindowSwapchain;

class VulkanRenderer;

class VulkanSwapchain
{
	public:

		Window *mainWindow;
		std::vector<Window*> extraWindows;

		std::map<Window*, WindowSwapchain> swapchains;

		VulkanSwapchain (VulkanRenderer* vulkanRendererParent);
		virtual ~VulkanSwapchain ();

		void init();
		void initSwapchain(Window *wnd);
		void presentToSwapchain(Window *wnd);

		void createSwapchain(Window *wnd);
		void destroySwapchain(Window *wnd);
		void recreateSwapchain(Window *wnd);

		void setSwapchainSourceImage(Window *wnd, VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout, bool rewriteCommandBuffers = true);

		SwapchainSupportDetails querySwapchainSupport (VkPhysicalDevice device, VkSurfaceKHR surface);

	private:

		VkCommandPool swapchainCommandPool;

		VkImage swapchainDummySourceImage;
		VkImageView swapchainDummySourceImageView;
		VmaAllocation swapchainDummySourceImageMemory;
		VkSampler swapchainDummySampler;

		VkDescriptorSetLayout swapchainDescriptorLayout;

		VkShaderModule swapchainVertShader;
		VkShaderModule swapchainFragShader;

		VkPipelineLayout swapchainPipelineLayout;

		VulkanRenderer* renderer;

		void createDescriptors(WindowSwapchain &swapchain);
		void createFramebuffers(WindowSwapchain &swapchain);
		void createSyncPrimitives(WindowSwapchain &swapchain);
		void prerecordCommandBuffers(WindowSwapchain &swapchain);

		void createRenderPass(WindowSwapchain &swapchain);
		void createGraphicsPipeline(WindowSwapchain &swapchain);

		void createDummySourceImage ();

		VkSurfaceFormatKHR chooseSwapSurfaceFormat (const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR chooseSwapPresentMode (const std::vector<VkPresentModeKHR> availablePresentModes);
		VkExtent2D chooseSwapExtent (const VkSurfaceCapabilitiesKHR& capabilities, uint32_t windowWidth, uint32_t windowHeight);
};

#endif /* RENDERING_VULKAN_VULKANSWAPCHAIN_H_ */
