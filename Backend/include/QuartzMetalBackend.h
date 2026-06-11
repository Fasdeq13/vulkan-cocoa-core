#ifndef QUARTZ_METAL_BACKEND_H
#define QUARTZ_METAL_BACKEND_H

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>

struct BackendDeviceCaps {
    std::string deviceName;
    uint32_t vendorID;
    uint32_t deviceID;
    bool supportsArgumentBuffers;
    bool supportsBindless;
};

class AIRToSPIRVCompiler {
public:
    AIRToSPIRVCompiler() = default;
    ~AIRToSPIRVCompiler() = default;
    bool compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode, std::vector<uint32_t>& outSpirv);
};

class VulkanRenderContext {
private:
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamilyIndex;
    BackendDeviceCaps caps;

    void selectPhysicalDevice();
    void createLogicalDevice();

public:
    VulkanRenderContext();
    ~VulkanRenderContext();

    void initContext();
    VkDevice getVkDevice() const { return device; }
    VkPhysicalDevice getVkPhysicalDevice() const { return physicalDevice; }
    VkQueue getVkGraphicsQueue() const { return graphicsQueue; }
    uint32_t getQueueFamilyIndex() const { return graphicsQueueFamilyIndex; }
    const BackendDeviceCaps& getDeviceCaps() const { return caps; }

    VkDeviceMemory allocateVideoMemory(VkBuffer buffer, VkMemoryPropertyFlags properties);
    VkDeviceMemory allocateImageMemory(VkImage image, VkMemoryPropertyFlags properties);
};

#ifdef __cplusplus
extern "C" {
#endif
    VulkanRenderContext* GetGlobalVulkanContext();
#ifdef __cplusplus
}
#endif

#endif // QUARTZ_METAL_BACKEND_H
