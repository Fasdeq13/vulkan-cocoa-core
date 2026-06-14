#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#define RAVYN_MAX_FRAMES_IN_FLIGHT 3

struct VulkanRenderContext {
    VkInstance               instance;
    VkPhysicalDevice         physicalDevice;
    VkDevice                 device;

    VkQueue                  graphicsQueue;
    VkQueue                  transferQueue;
    VkQueue                  computeQueue;

    uint32_t                 graphicsQueueFamilyIndex;
    uint32_t                 transferQueueFamilyIndex;

    VkSurfaceKHR             surface         = VK_NULL_HANDLE;
    VkSwapchainKHR           swapchain       = VK_NULL_HANDLE;
    VkRenderPass             renderPass      = VK_NULL_HANDLE;
    VkCommandPool            commandPool     = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger  = VK_NULL_HANDLE;

    VkFormat                 swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               swapchainExtent = { 0, 0 };

    std::vector<VkImage>       swapchainImages;
    std::vector<VkImageView>   swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore>   imageAvailableSemaphores;
    std::vector<VkSemaphore>   renderFinishedSemaphores;
    std::vector<VkFence>       inFlightFences;

    uint32_t                 currentFrame;
    uint32_t                 monitorCount;

    VulkanRenderContext();
    ~VulkanRenderContext();

    void initContext();
    void createSwapchain(VkInstance inst, VkPhysicalDevice phys, VkDevice dev,
                         uint32_t w, uint32_t h);
    VkImage         importExternalMemory(int fd, uint32_t w, uint32_t h);
    VkDeviceMemory  allocateVideoMemory(VkMemoryRequirements req, VkMemoryPropertyFlags props);
    void            recordAndSubmitFrame(uint32_t imageIndex);
    void            presentFrame(uint32_t imageIndex);
};

struct AIRToSPIRVCompiler {
    bool compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode,
                            std::vector<uint32_t>& outSpirv);
};

VulkanRenderContext* GetGlobalVulkanContext();
uint32_t findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter,
                         VkMemoryPropertyFlags props);
void presentNextFrame(VulkanRenderContext* ctx);
