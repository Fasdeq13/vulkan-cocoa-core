#ifndef QUARTZ_METAL_BACKEND_H
#define QUARTZ_METAL_BACKEND_H
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#ifndef CGGEOMETRY_H_
struct CGPoint {
    double x;
    double y;
};
struct CGSize {
    double width;
    double height;
};
struct CGRect {
    struct CGPoint origin;
    struct CGSize size;
};
#endif
typedef double CGFloat;
typedef uint32_t CGGlyph;
typedef struct CGColor* CGColorRef;
typedef struct CGContext* CGContextRef;
typedef struct CGFont* CGFontRef;
typedef struct CGImage* CGImageRef;
typedef struct BoxColorSpace* CGColorSpaceRef;
typedef const struct __CFString* CFStringRef;
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
    VkQueue transferQueue;
    VkSwapchainKHR swapchain;
    uint32_t graphicsQueueFamilyIndex;
    uint32_t transferQueueFamilyIndex;
    BackendDeviceCaps caps;
    void selectPhysicalDevice();
    void createLogicalDevice();
public:
    VulkanRenderContext();
    ~VulkanRenderContext();
    void initContext();
    void createSwapchain(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t w, uint32_t h);
    VkImage importExternalMemory(int fd, uint32_t w, uint32_t h);
    VkDeviceMemory allocateVideoMemory(VkMemoryRequirements req, VkMemoryPropertyFlags props);
    VkDeviceMemory allocateImageMemory(VkImage image, VkMemoryPropertyFlags properties);
    void buildDynamicObjectiveCBridge();
    VkInstance getVkInstance() const { return instance; }
    VkDevice getVkDevice() const { return device; }
    VkPhysicalDevice getVkPhysicalDevice() const { return physicalDevice; }
    VkQueue getVkGraphicsQueue() const { return graphicsQueue; }
    VkQueue getVkTransferQueue() const { return transferQueue; }
    uint32_t getQueueFamilyIndex() const { return graphicsQueueFamilyIndex; }
    const BackendDeviceCaps& getDeviceCaps() const { return caps; }
    void setVkPhysicalDevice(VkPhysicalDevice pd) { physicalDevice = pd; }
    void setVkDevice(VkDevice d) { device = d; }
    void setVkGraphicsQueue(VkQueue q) { graphicsQueue = q; }
};
#ifdef __cplusplus
extern "C" {
#endif
    VulkanRenderContext* GetGlobalVulkanContext();
    CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
    CGColorSpaceRef CGColorSpaceCreateDeviceGray(void);
    CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef name);
    void CGColorSpaceRelease(CGColorSpaceRef space);
    CGContextRef CGBitmapContextCreate(void* data, size_t width, size_t height, size_t bitsPerComponent, size_t bytesPerRow, CGColorSpaceRef space, uint32_t bitmapInfo);
    void CGContextRelease(CGContextRef c);
    void CGContextClearRect(CGContextRef c, CGRect rect);
    void CGContextSetRGBFillColor(CGContextRef c, CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha);
    void CGContextFillRect(CGContextRef c, CGRect rect);
    CGFontRef CGFontCreateWithDataProvider(void* provider);
    CGFontRef CGFontCreateWithFontName(CFStringRef name);
    void CGFontRelease(CGFontRef font);
    int NSApplicationMain(int argc, const char* argv[]);
#ifdef __cplusplus
}
#endif
#endif
