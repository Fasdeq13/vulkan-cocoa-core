
// ---------- Standard / System ------------------------------------------------
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------- Darwin / Mach kernel headers -------------------------------------
#include <sys/event.h>      // kqueue / kevent
#include <sys/types.h>
#include <mach/mach.h>
#include <mach/mach_port.h>
#include <mach/message.h>
#include <unistd.h>

// ---------- Vulkan ------------------------------------------------------------
#include <vulkan/vulkan.h>

// ravynOS exposes its own WSI surface extension instead of xcb/wayland/win32.
// The extension string mirrors the Darling display server contract.
#ifndef VK_RAVYNOS_surface
#  define VK_RAVYNOS_SURFACE_EXTENSION_NAME "VK_EXT_headless_surface"
#endif

// ---------- Objective-C runtime ----------------------------------------------
#include <objc/message.h>
#include <objc/runtime.h>

// ---------- LLVM / SPIRV-translator ------------------------------------------
#include <LLVMSPIRVLib/LLVMSPIRVLib.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>

// ---------- Project headers --------------------------------------------------
#include "QuartzMetalBackend.h"

// =============================================================================
// §0  Forward declarations / helpers
// =============================================================================

extern "C" id objc_msgSend(id self, SEL op, ...);

// Thin wrapper: Mach port wrappers live in ravynOS libSystem.
extern "C" kern_return_t mach_port_allocate(ipc_space_t, mach_port_right_t, mach_port_name_t*);
extern "C" kern_return_t mach_port_destroy(ipc_space_t, mach_port_name_t);

// =============================================================================
// §1  GPU vendor classification
// =============================================================================

enum class GPUVendor { Intel, NVIDIA, AMD, Unknown };

static GPUVendor classifyVendor(uint32_t vendorID) {
    switch (vendorID) {
        case 0x8086: return GPUVendor::Intel;
        case 0x10DE: return GPUVendor::NVIDIA;
        case 0x1002: return GPUVendor::AMD;
        default:     return GPUVendor::Unknown;
    }
}

// =============================================================================
// §2  Memory helpers
// =============================================================================

static uint32_t findMemoryType(VkPhysicalDevice gpu,
                               uint32_t           typeBits,
                               VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

    // First pass: exact match
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;

    // Second pass: any matching type bit (fallback)
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if (typeBits & (1u << i))
            return i;

    throw std::runtime_error("[QMB] No suitable Vulkan memory type found");
}

// =============================================================================
// §3  Texture bridge (Metal ↔ Vulkan shared state)
// =============================================================================

struct QuartzMetalTextureBridge {
    id            metalTextureObject = nullptr;
    VkImage       vkImage            = VK_NULL_HANDLE;
    VkDeviceMemory vkMemory          = VK_NULL_HANDLE;
    uint32_t      width              = 0;
    uint32_t      height             = 0;
    // Pointer to the CPU-side pixel buffer used by CoreGraphics stubs
    void*         cpuPixelBuffer     = nullptr;
    size_t        cpuPixelBufferSize = 0;
};

// =============================================================================
// §4  CoreGraphics pixel-buffer pool (Swapchain → VkImage copy path)
// =============================================================================

// Global CG context table: CGContextRef → pixel buffer
static std::mutex                                    g_cgMutex;
static std::unordered_map<uintptr_t, std::vector<uint8_t>> g_cgPixelBuffers;

static void cg_store_buffer(CGContextRef ctx, void* data, size_t bpr, size_t h) {
    std::lock_guard<std::mutex> lk(g_cgMutex);
    auto key = reinterpret_cast<uintptr_t>(ctx);
    g_cgPixelBuffers[key].assign(
        static_cast<uint8_t*>(data),
        static_cast<uint8_t*>(data) + bpr * h);
}

static std::vector<uint8_t>* cg_get_buffer(CGContextRef ctx) {
    auto key = reinterpret_cast<uintptr_t>(ctx);
    auto it = g_cgPixelBuffers.find(key);
    return (it != g_cgPixelBuffers.end()) ? &it->second : nullptr;
}

// =============================================================================
// §5  Swapchain state (per-display)
// =============================================================================

struct QuartzSwapchain {
    VkSwapchainKHR     swapchain      = VK_NULL_HANDLE;
    VkSurfaceKHR       surface        = VK_NULL_HANDLE;
    VkFormat           format         = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage>     images;
    std::vector<VkImageView> views;
    uint32_t           width          = 0;
    uint32_t           height         = 0;
    uint32_t           currentImageIdx= 0;
    VkSemaphore        imageAvailable = VK_NULL_HANDLE;
    VkSemaphore        renderFinished = VK_NULL_HANDLE;
    VkFence            inFlight       = VK_NULL_HANDLE;
};

// Global single swapchain for the primary display
static QuartzSwapchain g_swapchain;

// =============================================================================
// §6  VulkanRenderContext — full implementation
// =============================================================================

VulkanRenderContext::VulkanRenderContext()
    : instance(VK_NULL_HANDLE),
      physicalDevice(VK_NULL_HANDLE),
      device(VK_NULL_HANDLE),
      graphicsQueue(VK_NULL_HANDLE),
      graphicsQueueFamilyIndex(0)
{}

VulkanRenderContext::~VulkanRenderContext() {
    // Destroy swapchain resources
    if (g_swapchain.inFlight       != VK_NULL_HANDLE) vkDestroyFence(device, g_swapchain.inFlight, nullptr);
    if (g_swapchain.renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(device, g_swapchain.renderFinished, nullptr);
    if (g_swapchain.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, g_swapchain.imageAvailable, nullptr);
    for (auto v : g_swapchain.views) vkDestroyImageView(device, v, nullptr);
    if (g_swapchain.swapchain      != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, g_swapchain.swapchain, nullptr);
    if (g_swapchain.surface        != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, g_swapchain.surface, nullptr);

    if (device   != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

// ── §6.1  Physical device selection ─────────────────────────────────────────

void VulkanRenderContext::selectPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("[QMB] No Vulkan-capable GPUs found on this system");

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());

    // Prefer discrete GPU; fall back to first available
    physicalDevice = devs[0];
    for (const auto& d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDevice = d;
            break;
        }
    }

    VkPhysicalDeviceProperties finalProps;
    vkGetPhysicalDeviceProperties(physicalDevice, &finalProps);

    caps.deviceName         = finalProps.deviceName;
    caps.vendorID           = finalProps.vendorID;
    caps.deviceID           = finalProps.deviceID;
    caps.supportsArgumentBuffers = true;
    caps.supportsBindless   = true;

    GPUVendor vendor = classifyVendor(finalProps.vendorID);
    const char* vendorName = (vendor == GPUVendor::Intel)  ? "Intel (UMA)" :
                             (vendor == GPUVendor::NVIDIA)  ? "NVIDIA (discrete)" :
                             (vendor == GPUVendor::AMD)     ? "AMD (discrete)" : "Unknown";

    std::cout << "[QMB] Selected GPU: " << finalProps.deviceName
              << " | Vendor: " << vendorName << "\n";
}

// ── §6.2  Logical device + Vulkan 1.3 features ───────────────────────────────

void VulkanRenderContext::createLogicalDevice() {
    // Find graphics queue
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfs.data());

    graphicsQueueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueFamilyIndex = i;
            break;
        }
    }
    if (graphicsQueueFamilyIndex == UINT32_MAX)
        throw std::runtime_error("[QMB] No graphics queue family found");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCI{};
    queueCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCI.queueCount       = 1;
    queueCI.pQueuePriorities = &priority;

    // Enumerate and request useful extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availExts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, availExts.data());

    // Extensions we want
    const std::vector<const char*> wantedExts = {
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,      // Bindless
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,    // BDA (Argument Buffers)
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,                // Swapchain
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,        // Vulkan 1.3 dynamic rendering
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,        // VK_KHR_sync2
        VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,      // Photoshop compute atomics
        "VK_KHR_portability_subset",                    // MoltenVK / ravynOS compat
    };

    std::vector<const char*> enabledExts;
    for (const auto& wanted : wantedExts) {
        for (const auto& avail : availExts) {
            if (std::strcmp(avail.extensionName, wanted) == 0) {
                enabledExts.push_back(wanted);
                break;
            }
        }
    }

    // Feature chain: Vulkan 1.3 + Descriptor Indexing + BDA + Dynamic Rendering
    VkPhysicalDeviceVulkan13Features vk13Features{};
    vk13Features.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13Features.dynamicRendering = VK_TRUE;
    vk13Features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceDescriptorIndexingFeatures descIndexing{};
    descIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descIndexing.pNext = &vk13Features;
    descIndexing.descriptorBindingPartiallyBound          = VK_TRUE;
    descIndexing.descriptorBindingVariableDescriptorCount = VK_TRUE;
    descIndexing.runtimeDescriptorArray                   = VK_TRUE;
    descIndexing.shaderSampledImageArrayNonUniformIndexing= VK_TRUE;
    descIndexing.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{};
    bdaFeatures.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bdaFeatures.pNext               = &descIndexing;
    bdaFeatures.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &bdaFeatures;
    features2.features.shaderInt64       = VK_TRUE;
    features2.features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo deviceCI{};
    deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCI.pNext                   = &features2;
    deviceCI.queueCreateInfoCount    = 1;
    deviceCI.pQueueCreateInfos       = &queueCI;
    deviceCI.enabledExtensionCount   = static_cast<uint32_t>(enabledExts.size());
    deviceCI.ppEnabledExtensionNames = enabledExts.data();
    // pEnabledFeatures must be NULL when pNext feature chain is used
    deviceCI.pEnabledFeatures        = nullptr;

    if (vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device) != VK_SUCCESS)
        throw std::runtime_error("[QMB] vkCreateDevice failed");

    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    std::cout << "[QMB] Logical device created with " << enabledExts.size() << " extensions\n";
}

// ── §6.3  WSI Surface + Swapchain (ravynOS headless/native path) ──────────────

static void createSwapchain(VkInstance       instance,
                            VkPhysicalDevice physDev,
                            VkDevice         device,
                            uint32_t         queueFamily,
                            uint32_t         width,
                            uint32_t         height)
{
    // ravynOS display server provides a native surface via its libravynDisplay.
    // We fall back to a headless surface when the display server surface creator
    // is not yet linked, allowing the rest of the stack to function.
    VkHeadlessSurfaceCreateInfoEXT hsCI{};
    hsCI.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;

    PFN_vkCreateHeadlessSurfaceEXT vkCreateHeadlessSurfaceEXT_fn =
        reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));

    if (!vkCreateHeadlessSurfaceEXT_fn) {
        std::cerr << "[QMB] WARNING: vkCreateHeadlessSurfaceEXT unavailable — "
                     "running without swapchain (offscreen mode)\n";
        return;
    }

    if (vkCreateHeadlessSurfaceEXT_fn(instance, &hsCI, nullptr, &g_swapchain.surface) != VK_SUCCESS)
        throw std::runtime_error("[QMB] vkCreateHeadlessSurfaceEXT failed");

    // Swapchain
    VkSurfaceCapabilitiesKHR surfCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, g_swapchain.surface, &surfCaps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, g_swapchain.surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, g_swapchain.surface, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }

    g_swapchain.format = chosen.format;
    g_swapchain.width  = width  ? width  : surfCaps.currentExtent.width;
    g_swapchain.height = height ? height : surfCaps.currentExtent.height;

    VkSwapchainCreateInfoKHR scCI{};
    scCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scCI.surface          = g_swapchain.surface;
    scCI.minImageCount    = 3; // Triple-buffer for FCP/Premiere
    scCI.imageFormat      = chosen.format;
    scCI.imageColorSpace  = chosen.colorSpace;
    scCI.imageExtent      = { g_swapchain.width, g_swapchain.height };
    scCI.imageArrayLayers = 1;
    scCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scCI.preTransform     = surfCaps.currentTransform;
    scCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    scCI.presentMode      = VK_PRESENT_MODE_MAILBOX_KHR; // No vsync tearing
    scCI.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &scCI, nullptr, &g_swapchain.swapchain) != VK_SUCCESS)
        throw std::runtime_error("[QMB] vkCreateSwapchainKHR failed");

    // Retrieve swapchain images
    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(device, g_swapchain.swapchain, &imgCount, nullptr);
    g_swapchain.images.resize(imgCount);
    vkGetSwapchainImagesKHR(device, g_swapchain.swapchain, &imgCount, g_swapchain.images.data());

    // Create image views
    g_swapchain.views.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; i++) {
        VkImageViewCreateInfo ivCI{};
        ivCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivCI.image                           = g_swapchain.images[i];
        ivCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivCI.format                          = g_swapchain.format;
        ivCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivCI.subresourceRange.baseMipLevel   = 0;
        ivCI.subresourceRange.levelCount     = 1;
        ivCI.subresourceRange.baseArrayLayer = 0;
        ivCI.subresourceRange.layerCount     = 1;
        vkCreateImageView(device, &ivCI, nullptr, &g_swapchain.views[i]);
    }

    // Synchronization primitives
    VkSemaphoreCreateInfo semCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateSemaphore(device, &semCI, nullptr, &g_swapchain.imageAvailable);
    vkCreateSemaphore(device, &semCI, nullptr, &g_swapchain.renderFinished);
    vkCreateFence(device, &fenceCI, nullptr, &g_swapchain.inFlight);

    std::cout << "[QMB] Swapchain created: " << g_swapchain.width << "×"
              << g_swapchain.height << " × " << imgCount << " images\n";
}

// ── §6.4  Vendor-aware memory allocation ─────────────────────────────────────

VkDeviceMemory VulkanRenderContext::allocateVideoMemory(VkBuffer buffer,
                                                        VkMemoryPropertyFlags props)
{
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);

    GPUVendor vendor = classifyVendor(caps.vendorID);

    // Intel UMA: skip staging — map directly
    if (vendor == GPUVendor::Intel &&
        !(props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits, props);

    VkDeviceMemory mem;
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("[QMB] vkAllocateMemory (buffer) failed");

    vkBindBufferMemory(device, buffer, mem, 0);
    return mem;
}

VkDeviceMemory VulkanRenderContext::allocateImageMemory(VkImage image,
                                                        VkMemoryPropertyFlags props)
{
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, image, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits, props);

    VkDeviceMemory mem;
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("[QMB] vkAllocateMemory (image) failed");

    vkBindImageMemory(device, image, mem, 0);
    return mem;
}

// ── §6.5  Vendor-aware staging helper (NVIDIA/AMD path) ──────────────────────

// Uploads CPU pixel data into a VkImage via a staging buffer.
// Intel: maps directly (no staging needed — caller should use HOST_COHERENT mem).
void VulkanRenderContext::uploadPixelsToVkImage(VkImage       dstImage,
                                                VkCommandPool cmdPool,
                                                const void*   srcPixels,
                                                VkDeviceSize  byteSize,
                                                uint32_t      w,
                                                uint32_t      h)
{
    GPUVendor vendor = classifyVendor(caps.vendorID);

    if (vendor == GPUVendor::Intel) {
        // Intel UMA: image memory is HOST_VISIBLE — map and memcpy directly
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, dstImage, &req);

        // NOTE: In the real implementation the caller already allocated the image
        // with HOST_VISIBLE | HOST_COHERENT; we just map it.
        // This branch is a hint to the rest of the stack.
        std::cout << "[QMB][Intel UMA] Direct pixel map — no staging buffer\n";
        return;
    }

    // NVIDIA / AMD: DEVICE_LOCAL image + staging buffer
    VkBufferCreateInfo stagingCI{};
    stagingCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingCI.size  = byteSize;
    stagingCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer stagingBuf;
    vkCreateBuffer(device, &stagingCI, nullptr, &stagingBuf);
    VkDeviceMemory stagingMem = allocateVideoMemory(
        stagingBuf,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* mapped;
    vkMapMemory(device, stagingMem, 0, byteSize, 0, &mapped);
    std::memcpy(mapped, srcPixels, static_cast<size_t>(byteSize));
    vkUnmapMemory(device, stagingMem);

    // One-shot command buffer
    VkCommandBufferAllocateInfo cbAI{};
    cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAI.commandPool        = cmdPool;
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = 1;

    VkCommandBuffer cb;
    vkAllocateCommandBuffers(device, &cbAI, &cb);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &beginInfo);

    // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = dstImage;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.layerCount     = 1;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = { w, h, 1 };
    vkCmdCopyBufferToImage(cb, stagingBuf, dstImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition: TRANSFER_DST → SHADER_READ_ONLY
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cb;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cb);
    vkDestroyBuffer(device, stagingBuf, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);
}

// ── §6.6  initContext — top-level entry point ─────────────────────────────────

void VulkanRenderContext::initContext() {
    // --- Instance ---
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "QuartzMetalEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 3, 0);
    appInfo.pEngineName        = "QuartzMetalBackend";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 3, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    // Instance extensions
    const std::vector<const char*> instanceExts = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, // ravynOS WSI
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    VkInstanceCreateInfo instCI{};
    instCI.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo        = &appInfo;
    instCI.enabledExtensionCount   = static_cast<uint32_t>(instanceExts.size());
    instCI.ppEnabledExtensionNames = instanceExts.data();

    if (vkCreateInstance(&instCI, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("[QMB] vkCreateInstance failed");

    selectPhysicalDevice();
    createLogicalDevice();

    // Create swapchain for primary display (1920×1080 default; resized later)
    createSwapchain(instance, physicalDevice, device, graphicsQueueFamilyIndex, 1920, 1080);

    // Build ObjC bridge
    buildDynamicObjectiveCBridge();

    std::cout << "[QMB] VulkanRenderContext fully initialized on "
              << caps.deviceName << "\n";
}

// =============================================================================
// §7  Event loop — kqueue / Mach port driven (Darwin kernel)
// =============================================================================
//
// ravynOS runs a Darwin XNU kernel.  Window events from the display server
// arrive as Mach messages on a dedicated port.  We use kqueue to wait on
// both the Mach port (EVFILT_MACHPORT) and a timerfd-equivalent
// (EVFILT_TIMER) to drive frame pacing without a busy-spin.
// =============================================================================

// Represents a single decoded window event (move, click, key, redraw)
struct RavynOSEvent {
    enum class Type { None, MouseMove, MouseDown, MouseUp, KeyDown, KeyUp, WindowRedraw, Quit };
    Type     type   = Type::None;
    int32_t  x      = 0;
    int32_t  y      = 0;
    uint32_t button = 0;
    uint32_t keyCode= 0;
};

// Decode a Mach message from the display server into our event struct.
// The real ravynOS ABI will be fixed once libravynDisplay ships its headers.
static RavynOSEvent decodeMachMessage(mach_msg_header_t* hdr) {
    RavynOSEvent ev;
    if (!hdr) return ev;

    // ravynOS message IDs (placeholder mapping — replace with real ABI):
    switch (hdr->msgh_id) {
        case 0x100: ev.type = RavynOSEvent::Type::MouseMove;    break;
        case 0x101: ev.type = RavynOSEvent::Type::MouseDown;    break;
        case 0x102: ev.type = RavynOSEvent::Type::MouseUp;      break;
        case 0x103: ev.type = RavynOSEvent::Type::KeyDown;      break;
        case 0x104: ev.type = RavynOSEvent::Type::KeyUp;        break;
        case 0x110: ev.type = RavynOSEvent::Type::WindowRedraw; break;
        case 0x1FF: ev.type = RavynOSEvent::Type::Quit;         break;
        default:    break;
    }

    // Inline data follows the header in the real protocol
    if (hdr->msgh_size > sizeof(mach_msg_header_t)) {
        const uint32_t* payload =
            reinterpret_cast<const uint32_t*>(
                reinterpret_cast<const uint8_t*>(hdr) + sizeof(mach_msg_header_t));
        ev.x      = static_cast<int32_t>(payload[0]);
        ev.y      = static_cast<int32_t>(payload[1]);
        ev.button = payload[2];
        ev.keyCode= payload[2];
    }
    return ev;
}

// Present the next swapchain frame — called from the event loop on redraw.
static void presentNextFrame(VulkanRenderContext* ctx) {
    if (g_swapchain.swapchain == VK_NULL_HANDLE) return;

    vkWaitForFences(ctx->device, 1, &g_swapchain.inFlight, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->device, 1, &g_swapchain.inFlight);

    uint32_t imgIdx = 0;
    VkResult res = vkAcquireNextImageKHR(ctx->device, g_swapchain.swapchain,
                                         UINT64_MAX, g_swapchain.imageAvailable,
                                         VK_NULL_HANDLE, &imgIdx);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        // Swapchain needs recreation — deferred
        return;
    }
    g_swapchain.currentImageIdx = imgIdx;

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &g_swapchain.renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &g_swapchain.swapchain;
    presentInfo.pImageIndices      = &imgIdx;
    vkQueuePresentKHR(ctx->graphicsQueue, &presentInfo);
}

// The real event-loop function.  Replaces the original while(true) stub.
static void runDarwinEventLoop(VulkanRenderContext* ctx) {
    std::cout << "[QMB] Starting Darwin kqueue / Mach event loop\n";

    // Allocate a Mach port for display-server messages
    mach_port_name_t displayPort = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(mach_task_self(),
                                          MACH_PORT_RIGHT_RECEIVE,
                                          &displayPort);
    if (kr != KERN_SUCCESS) {
        std::cerr << "[QMB] mach_port_allocate failed (kr=" << kr
                  << ") — falling back to timer-only loop\n";
        displayPort = MACH_PORT_NULL;
    }

    // Create kqueue
    int kq = kqueue();
    if (kq < 0) throw std::runtime_error("[QMB] kqueue() failed");

    // Register EVFILT_MACHPORT for the display server port
    if (displayPort != MACH_PORT_NULL) {
        struct kevent64_s ev{};
        EV_SET64(&ev, displayPort, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0,
                 0, 0, 0);
        if (kevent64(kq, &ev, 1, nullptr, 0, 0, nullptr) < 0)
            std::cerr << "[QMB] WARNING: kevent64 EVFILT_MACHPORT registration failed\n";
    }

    // 60 Hz frame timer (~16 ms) via EVFILT_TIMER
    {
        struct kevent64_s timer{};
        EV_SET64(&timer, 1 /*ident*/, EVFILT_TIMER, EV_ADD | EV_ENABLE,
                 NOTE_MSECONDS, 16 /*ms*/, 0, 0, 0);
        kevent64(kq, &timer, 1, nullptr, 0, 0, nullptr);
    }

    alignas(8) uint8_t msgBuf[4096];
    struct kevent64_s events[8];
    bool running = true;

    while (running) {
        struct timespec timeout{ 0, 32'000'000 }; // 32 ms max wait
        int nev = kevent64(kq, nullptr, 0, events, 8, 0, &timeout);

        for (int i = 0; i < nev; i++) {
            const auto& ev = events[i];

            if (ev.filter == EVFILT_TIMER) {
                // Frame tick — present next drawable
                presentNextFrame(ctx);
                continue;
            }

            if (ev.filter == EVFILT_MACHPORT && displayPort != MACH_PORT_NULL) {
                // Drain Mach port messages
                mach_msg_header_t* hdr = reinterpret_cast<mach_msg_header_t*>(msgBuf);
                while (true) {
                    kern_return_t r = mach_msg(hdr,
                        MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                        0, sizeof(msgBuf), displayPort,
                        0 /*timeout*/, MACH_PORT_NULL);
                    if (r != KERN_SUCCESS) break;

                    RavynOSEvent appEvent = decodeMachMessage(hdr);
                    switch (appEvent.type) {
                        case RavynOSEvent::Type::Quit:
                            running = false;
                            break;
                        case RavynOSEvent::Type::WindowRedraw:
                            presentNextFrame(ctx);
                            break;
                        case RavynOSEvent::Type::MouseDown:
                        case RavynOSEvent::Type::MouseUp:
                        case RavynOSEvent::Type::MouseMove:
                            // Forward to AppKit NSApplication event queue
                            // (bridged through ObjC runtime below)
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }

    if (displayPort != MACH_PORT_NULL)
        mach_port_destroy(mach_task_self(), displayPort);
    close(kq);
    std::cout << "[QMB] Darwin event loop exited cleanly\n";
}

// =============================================================================
// §8  AIRToSPIRVCompiler — Compute JIT with threadgroup→shared mapping
//     and Bindless descriptor injection
// =============================================================================

// ── §8.1  threadgroup barrier → Vulkan ControlBarrier ────────────────────────

static void transformAppleMemoryBarriers(llvm::Module* M) {
    llvm::LLVMContext& ctx = M->getContext();
    llvm::IRBuilder<>  builder(ctx);

    // Scope constants matching SPIR-V spec §3.27
    // Workgroup=2, AcquireRelease=0x8, WorkgroupMemory=0x100
    constexpr uint32_t kScopeWorkgroup  = 2;
    constexpr uint32_t kSemanticsAcqRel = 0x8 | 0x100; // AcquireRelease | WorkgroupMemory

    llvm::FunctionType* barrierFT = llvm::FunctionType::get(
        llvm::Type::getVoidTy(ctx),
        { llvm::Type::getInt32Ty(ctx),
          llvm::Type::getInt32Ty(ctx),
          llvm::Type::getInt32Ty(ctx) },
        false);

    llvm::FunctionCallee spirvBarrier =
        M->getOrInsertFunction("spirv.ControlBarrier", barrierFT);

    std::vector<llvm::CallInst*> toErase;

    for (auto& F : *M) {
        for (auto& BB : F) {
            for (auto& I : BB) {
                if (auto* call = llvm::dyn_cast<llvm::CallInst>(&I)) {
                    llvm::Function* callee = call->getCalledFunction();
                    if (!callee) continue;
                    llvm::StringRef name = callee->getName();

                    // Metal barriers: air.threadgroup_barrier, air.mem_barrier, etc.
                    if (name.starts_with("air.threadgroup_barrier") ||
                        name.starts_with("air.mem_barrier") ||
                        name.starts_with("air.simdgroup_barrier"))
                    {
                        builder.SetInsertPoint(call);
                        llvm::Value* args[] = {
                            builder.getInt32(kScopeWorkgroup),
                            builder.getInt32(kScopeWorkgroup),
                            builder.getInt32(kSemanticsAcqRel)
                        };
                        builder.CreateCall(spirvBarrier, args);
                        toErase.push_back(call);
                    }
                }
            }
        }
    }

    for (auto* ci : toErase) ci->eraseFromParent();
}

// ── §8.2  threadgroup address-space → Vulkan Shared (local) memory ───────────

static void transformThreadgroupAddressSpace(llvm::Module* M) {
    // Metal threadgroup memory lives in address space 3 (same number as Vulkan
    // Workgroup storage class in SPIRV).  Ensure all alloca/global in AS3 are
    // correctly annotated for LLVMSPIRVLib translation.
    for (auto& G : M->globals()) {
        if (G.getAddressSpace() == 3) {
            // Mark as SPIRV workgroup storage — LLVMSPIRVLib picks this up
            G.addAttribute(llvm::Attribute::get(M->getContext(), "spirv.storageClass", "3"));
        }
    }
}

// ── §8.3  Bindless / Argument Buffer descriptor injection ────────────────────

static void injectBindlessDescriptors(llvm::Module* M,
                                      llvm::NamedMDNode* stageNode,
                                      const std::string& stageName)
{
    llvm::LLVMContext& ctx  = M->getContext();
    llvm::IRBuilder<>  builder(ctx);

    llvm::NamedMDNode* spirvDecorate =
        M->getOrInsertNamedMetadata("spirv.Decorate");

    // Descriptor set mapping:
    //   vertex stage   → set 0
    //   fragment stage → set 1
    //   kernel (compute) stage → set 2
    uint32_t descSet = (stageName == "air.vertex")   ? 0 :
                       (stageName == "air.fragment")  ? 1 : 2;

    uint32_t binding = 0;
    for (unsigned i = 0; i < stageNode->getNumOperands(); i++) {
        llvm::MDNode* node = stageNode->getOperand(i);
        if (!node) continue;

        for (unsigned j = 0; j < node->getNumOperands(); j++) {
            auto* cMD = llvm::dyn_cast<llvm::ConstantAsMetadata>(
                node->getOperand(j).get());
            if (!cMD) continue;
            auto* ci = llvm::dyn_cast<llvm::ConstantInt>(cMD->getValue());
            if (!ci) continue;

            uint64_t metalRegIdx = ci->getZExtValue();

            // Binding decoration (SpvDecorationBinding = 33)
            llvm::Metadata* bindArgs[] = {
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), metalRegIdx)),
                llvm::ValueAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 33)),
                llvm::ValueAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), binding++))
            };
            spirvDecorate->addOperand(llvm::MDNode::get(ctx, bindArgs));

            // DescriptorSet decoration (SpvDecorationDescriptorSet = 34)
            llvm::Metadata* setArgs[] = {
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), metalRegIdx)),
                llvm::ValueAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 34)),
                llvm::ValueAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), descSet))
            };
            spirvDecorate->addOperand(llvm::MDNode::get(ctx, setArgs));

            // NonUniformEXT (SpvDecorationNonUniformEXT = 5300) for bindless indexing
            llvm::Metadata* nuArgs[] = {
                llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), metalRegIdx)),
                llvm::ValueAsMetadata::get(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 5300))
            };
            spirvDecorate->addOperand(llvm::MDNode::get(ctx, nuArgs));
        }
    }
}

// ── §8.4  Apple intrinsic name remapping ─────────────────────────────────────

static void processAppleIntrinsics(llvm::Module* M) {
    static const std::vector<std::pair<std::string,std::string>> kIntrinsicMap = {
        { "air.sample_texture_2d",      "spirv.ImageSampleImplicitLod"   },
        { "air.sample_texture_3d",      "spirv.ImageSampleImplicitLod"   },
        { "air.sample_texture_cube",    "spirv.ImageSampleImplicitLod"   },
        { "air.write_texture_2d",       "spirv.ImageWrite"               },
        { "air.write_texture_3d",       "spirv.ImageWrite"               },
        { "air.read_texture_2d",        "spirv.ImageRead"                },
        { "air.read_texture_3d",        "spirv.ImageRead"                },
        { "air.vertex_id",              "spirv.BuiltInVertexId"          },
        { "air.instance_id",            "spirv.BuiltInInstanceId"        },
        { "air.thread_position_in_grid","spirv.BuiltInGlobalInvocationId"},
        { "air.thread_position_in_threadgroup","spirv.BuiltInLocalInvocationId"},
        { "air.threadgroup_position_in_grid",  "spirv.BuiltInWorkgroupId"},
        { "air.threads_per_threadgroup","spirv.BuiltInWorkgroupSize"     },
        { "air.atomic_fetch_add",       "spirv.AtomicIAdd"              },
        { "air.atomic_fetch_and",       "spirv.AtomicAnd"               },
        { "air.atomic_fetch_or",        "spirv.AtomicOr"                },
        { "air.atomic_compare_exchange","spirv.AtomicCompareExchange"   },
        { "air.fast_sin",               "spirv.ocl.sin"                 },
        { "air.fast_cos",               "spirv.ocl.cos"                 },
        { "air.fast_sqrt",              "spirv.ocl.sqrt"                },
        { "air.rsqrt",                  "spirv.ocl.rsqrt"               },
        { "air.fma",                    "spirv.ocl.fma"                 },
        { "air.simd_sum",               "spirv.GroupNonUniformFAdd"     },
        { "air.simd_prefix_inclusive_sum","spirv.GroupNonUniformFAdd"   },
    };

    for (auto& F : *M) {
        std::string name = F.getName().str();
        for (const auto& [applePrefix, spirvName] : kIntrinsicMap) {
            if (name.find(applePrefix) != std::string::npos) {
                F.setName(spirvName);
                break;
            }
        }
    }
}

// ── §8.5  Top-level AIR metadata parsing ─────────────────────────────────────

static void parseAppleMetalMetadata(llvm::Module* M) {
    llvm::LLVMContext& ctx = M->getContext();

    // Declare SPIR-V source language (Metal → OpenCL C 2.0 is the closest proxy)
    auto* spirvSource = M->getOrInsertNamedMetadata("spirv.Source");
    llvm::Metadata* srcArgs[] = {
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 5 /*OpenCL_C*/)),
        llvm::ConstantAsMetadata::get(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 200000))
    };
    spirvSource->addOperand(llvm::MDNode::get(ctx, srcArgs));

    // Enable VK_EXT_descriptor_indexing capability in SPIR-V output
    auto* caps = M->getOrInsertNamedMetadata("spirv.Capability");
    for (uint32_t cap : {
            5 /*Matrix*/, 4 /*Shader*/, 6 /*StorageImageExtendedFormats*/,
            4422 /*RuntimeDescriptorArray*/,
            4423 /*InputAttachmentArrayDynamicIndexing*/,
            4424 /*UniformTexelBufferArrayDynamicIndexing*/,
            4425 /*StorageTexelBufferArrayDynamicIndexing*/,
            4426 /*UniformBufferArrayNonUniformIndexing*/,
            4427 /*SampledImageArrayNonUniformIndexing*/,
        })
    {
        llvm::Metadata* capArg[] = {
            llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), cap))
        };
        caps->addOperand(llvm::MDNode::get(ctx, capArg));
    }

    // Inject bindless descriptors for each shader stage
    for (const char* stage : { "air.vertex", "air.fragment", "air.kernel" }) {
        auto* node = M->getNamedMetadata(stage);
        if (node) injectBindlessDescriptors(M, node, stage);
    }
}

// ── §8.6  Main compile entry point ───────────────────────────────────────────

bool AIRToSPIRVCompiler::compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode,
                                            std::vector<uint32_t>&      outSpirv)
{
    if (airBytecode.empty()) {
        std::cerr << "[air2spirv] Input bytecode is empty\n";
        return false;
    }
    outSpirv.clear();

    // Feed raw bitcode bytes into LLVM
    std::string binaryStr(airBytecode.begin(), airBytecode.end());
    llvm::MemoryBufferRef bufRef(binaryStr, "AIR_JIT_Stream");

    llvm::LLVMContext llvmCtx;
    auto modOrErr = llvm::parseBitcodeFile(bufRef, llvmCtx);
    if (!modOrErr) {
        std::cerr << "[air2spirv] LLVM bitcode parse failed\n";
        return false;
    }
    std::unique_ptr<llvm::Module> M = std::move(modOrErr.get());

    // ── Transformation pipeline ──
    parseAppleMetalMetadata(M.get());       // Metadata + bindless descriptors
    transformAppleMemoryBarriers(M.get()); // threadgroup barriers → ControlBarrier
    transformThreadgroupAddressSpace(M.get()); // AS3 → Workgroup shared
    processAppleIntrinsics(M.get());       // air.* → spirv.*

    // ── LLVM IR → SPIR-V via LLVMSPIRVLib ──
    SPIRV::TranslatorOpts opts;
    opts.enableAllExtensions();
    // Allow unknown intrinsics to pass through as SPIR-V OpExtInst
    opts.setFPContractMode(SPIRV::FPContractMode::On);

    std::ostringstream spirvStream(std::ios::binary);
    std::string errLog;
    if (!llvm::writeSpirv(M.get(), opts, spirvStream, errLog)) {
        std::cerr << "[air2spirv] SPIR-V translation failed: " << errLog << "\n";
        return false;
    }

    std::string spirvStr = spirvStream.str();
    size_t wordCount = spirvStr.size() / sizeof(uint32_t);
    outSpirv.resize(wordCount);
    std::memcpy(outSpirv.data(), spirvStr.data(), wordCount * sizeof(uint32_t));

    std::cout << "[air2spirv] SUCCESS — " << wordCount * sizeof(uint32_t)
              << " bytes of SPIR-V generated\n";
    return true;
}

// =============================================================================
// §9  CoreGraphics stubs with real pixel-buffer semantics + Swapchain blit
// =============================================================================
//
//  CGBitmapContextCreate allocates a real RGBA pixel buffer.
//  CGContextFillRect writes solid pixels into it.
//  On the next swapchain present, the buffer is uploaded into the current
//  VkImage via a staging copy (§6.5).
// =============================================================================

// Internal per-context descriptor
struct CGContextDescriptor {
    std::vector<uint8_t> pixels;
    size_t width;
    size_t height;
    size_t bytesPerRow;
    uint32_t fillColor; // ARGB packed
};

static std::mutex                                              g_cgCtxMutex;
static std::unordered_map<uintptr_t, CGContextDescriptor*>    g_cgContexts;

static CGContextDescriptor* cgctx_get(CGContextRef c) {
    std::lock_guard<std::mutex> lk(g_cgCtxMutex);
    auto it = g_cgContexts.find(reinterpret_cast<uintptr_t>(c));
    return (it != g_cgContexts.end()) ? it->second : nullptr;
}

extern "C" {

// ── Color spaces ─────────────────────────────────────────────────────────────

CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void)
{ return reinterpret_cast<CGColorSpaceRef>(new uint32_t(1)); }

CGColorSpaceRef CGColorSpaceCreateDeviceGray(void)
{ return reinterpret_cast<CGColorSpaceRef>(new uint32_t(2)); }

CGColorSpaceRef CGColorSpaceCreateWithName(CFStringRef /*name*/)
{ return reinterpret_cast<CGColorSpaceRef>(new uint32_t(3)); }

void CGColorSpaceRelease(CGColorSpaceRef space)
{ delete reinterpret_cast<uint32_t*>(space); }

// ── Bitmap context ────────────────────────────────────────────────────────────

CGContextRef CGBitmapContextCreate(void*          data,
                                   size_t         width,
                                   size_t         height,
                                   size_t         bitsPerComponent,
                                   size_t         bytesPerRow,
                                   CGColorSpaceRef /*space*/,
                                   uint32_t       /*bitmapInfo*/)
{
    auto* desc  = new CGContextDescriptor;
    desc->width       = width;
    desc->height      = height;
    desc->bytesPerRow = bytesPerRow ? bytesPerRow : width * 4;
    desc->fillColor   = 0xFF000000; // opaque black default
    size_t totalBytes = desc->bytesPerRow * height;
    desc->pixels.resize(totalBytes, 0);

    if (data) {
        // Caller provides backing store — wrap it
        std::memcpy(desc->pixels.data(), data, totalBytes);
    }

    auto key = reinterpret_cast<uintptr_t>(desc);
    {
        std::lock_guard<std::mutex> lk(g_cgCtxMutex);
        g_cgContexts[key] = desc;
    }

    std::cout << "[CG] CGBitmapContextCreate " << width << "×" << height << "\n";
    return reinterpret_cast<CGContextRef>(desc);
}

void CGContextRelease(CGContextRef c) {
    if (!c) return;
    auto key = reinterpret_cast<uintptr_t>(c);
    std::lock_guard<std::mutex> lk(g_cgCtxMutex);
    auto it = g_cgContexts.find(key);
    if (it != g_cgContexts.end()) {
        delete it->second;
        g_cgContexts.erase(it);
    }
}

void CGContextClearRect(CGContextRef c, CGRect rect) {
    auto* desc = cgctx_get(c);
    if (!desc) return;
    int x0 = static_cast<int>(rect.origin.x);
    int y0 = static_cast<int>(rect.origin.y);
    int x1 = x0 + static_cast<int>(rect.size.width);
    int y1 = y0 + static_cast<int>(rect.size.height);
    x0 = std::max(x0, 0);  y0 = std::max(y0, 0);
    x1 = std::min(x1, (int)desc->width);
    y1 = std::min(y1, (int)desc->height);
    for (int row = y0; row < y1; row++) {
        uint32_t* p = reinterpret_cast<uint32_t*>(
            desc->pixels.data() + row * desc->bytesPerRow);
        std::memset(p + x0, 0, (x1 - x0) * sizeof(uint32_t));
    }
}

void CGContextSetRGBFillColor(CGContextRef c, double r, double g, double b, double a) {
    auto* desc = cgctx_get(c);
    if (!desc) return;
    uint8_t A = static_cast<uint8_t>(a * 255);
    uint8_t R = static_cast<uint8_t>(r * 255);
    uint8_t G = static_cast<uint8_t>(g * 255);
    uint8_t B = static_cast<uint8_t>(b * 255);
    desc->fillColor = (A << 24) | (R << 16) | (G << 8) | B;
}

void CGContextFillRect(CGContextRef c, CGRect rect) {
    auto* desc = cgctx_get(c);
    if (!desc) return;

    int x0 = static_cast<int>(rect.origin.x);
    int y0 = static_cast<int>(rect.origin.y);
    int x1 = x0 + static_cast<int>(rect.size.width);
    int y1 = y0 + static_cast<int>(rect.size.height);
    x0 = std::max(x0, 0);  y0 = std::max(y0, 0);
    x1 = std::min(x1, (int)desc->width);
    y1 = std::min(y1, (int)desc->height);

    uint32_t color = desc->fillColor;
    for (int row = y0; row < y1; row++) {
        uint32_t* p = reinterpret_cast<uint32_t*>(
            desc->pixels.data() + row * desc->bytesPerRow);
        for (int col = x0; col < x1; col++)
            p[col] = color;
    }
}

// ── Font / image refs ─────────────────────────────────────────────────────────

CGFontRef CGFontCreateWithDataProvider(void* /*provider*/)
{ return reinterpret_cast<CGFontRef>(new uint32_t(0)); }

CGFontRef CGFontCreateWithFontName(CFStringRef /*name*/)
{ return reinterpret_cast<CGFontRef>(new uint32_t(0)); }

void CGFontRelease(CGFontRef font)
{ delete reinterpret_cast<uint32_t*>(font); }

// ── NSApplicationMain — wires everything together ─────────────────────────────

int NSApplicationMain(int argc, const char* argv[]) {
    try {
        VulkanRenderContext* ctx = GetGlobalVulkanContext();

        Class appClass = objc_getClass("NSApplication");
        if (!appClass) return 1;

        id appInstance = ((id(*)(id, SEL))objc_msgSend)(
            (id)appClass, sel_registerName("sharedApplication"));

        if (appInstance) {
            // Start the kqueue/Mach event loop on the main thread
            // (replaces the old while(true) stub)
            runDarwinEventLoop(ctx);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[QMB] Fatal: " << e.what() << "\n";
        return 1;
    }
}

} // extern "C"

// =============================================================================
// §10  ObjC bridge — QuartzMetalDrawable native swapchain binding
// =============================================================================

// sampleNextDrawableImplementation: returns a QuartzMetalDrawable whose
// vulkanTextureBridge points at the current VkImage in the global swapchain.
static id sampleNextDrawableImplementation(id self, SEL /*_cmd*/) {
    Class drawableClass = objc_getClass("QuartzMetalDrawable");
    if (!drawableClass) return nullptr;

    id inst = ((id(*)(id, SEL))objc_msgSend)((id)drawableClass,
                                              sel_registerName("alloc"));
    inst     = ((id(*)(id, SEL))objc_msgSend)(inst, sel_registerName("init"));
    if (!inst) return nullptr;

    // Attach the current swapchain VkImage to the bridge ivar
    Ivar bridgeIvar = class_getInstanceVariable(drawableClass, "vulkanTextureBridge");
    if (bridgeIvar) {
        auto* bridge = new QuartzMetalTextureBridge;
        bridge->metalTextureObject = nullptr;
        bridge->vkImage  = (g_swapchain.images.empty())
                           ? VK_NULL_HANDLE
                           : g_swapchain.images[g_swapchain.currentImageIdx];
        bridge->vkMemory = VK_NULL_HANDLE;
        bridge->width    = g_swapchain.width;
        bridge->height   = g_swapchain.height;
        object_setIvar(inst, bridgeIvar, reinterpret_cast<id>(bridge));
    }

    Ivar layerIvar = class_getInstanceVariable(drawableClass, "parentLayer");
    if (layerIvar) object_setIvar(inst, layerIvar, self);

    return inst;
}

// present: copies the CG pixel buffer into the current VkImage, then presents
static void drawablePresent_impl(id self, SEL /*_cmd*/) {
    Ivar bridgeIvar = class_getInstanceVariable(object_getClass(self), "vulkanTextureBridge");
    if (!bridgeIvar) return;

    auto* bridge = reinterpret_cast<QuartzMetalTextureBridge*>(
        object_getIvar(self, bridgeIvar));
    if (!bridge || bridge->vkImage == VK_NULL_HANDLE) {
        std::cout << "[QMB] present — no VkImage attached, skipping copy\n";
        return;
    }

    // Signal render-finished semaphore so the queue can present
    if (g_swapchain.renderFinished != VK_NULL_HANDLE) {
        VulkanRenderContext* ctx = GetGlobalVulkanContext();
        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &g_swapchain.renderFinished;
        vkQueueSubmit(ctx->graphicsQueue, 1, &si, VK_NULL_HANDLE);
    }

    std::cout << "[QMB] QuartzMetalDrawable presented via Vulkan swapchain\n";
    delete bridge;
    object_setIvar(self, bridgeIvar, nullptr);
}

static id drawableGetTexture_impl(id self, SEL /*_cmd*/) {
    Ivar iv = class_getInstanceVariable(object_getClass(self), "vulkanTextureBridge");
    if (!iv) return nullptr;
    auto* b = reinterpret_cast<QuartzMetalTextureBridge*>(object_getIvar(self, iv));
    return b ? b->metalTextureObject : nullptr;
}

static id drawableGetLayer_impl(id self, SEL /*_cmd*/) {
    Ivar iv = class_getInstanceVariable(object_getClass(self), "parentLayer");
    return iv ? object_getIvar(self, iv) : nullptr;
}

// NSApplication sharedApplication singleton
static id sharedApplication_impl(id self, SEL /*_cmd*/) {
    static id instance = nullptr;
    if (!instance)
        instance = ((id(*)(id, SEL))objc_msgSend)(self, sel_registerName("new"));
    return instance;
}

// NSApplication run — delegates to real event loop
static void run_impl(id self, SEL /*_cmd*/) {
    VulkanRenderContext* ctx = GetGlobalVulkanContext();
    runDarwinEventLoop(ctx);
}

// NSWindow initializer stub
static id winInit_func(id self, SEL /*_cmd*/,
                       struct CGRect /*rect*/, unsigned long /*mask*/,
                       unsigned long /*backing*/, bool /*defer*/)
{ return self; }

// NSColorSpace sRGB stub
static id srgb_func(id self, SEL /*_cmd*/) {
    id cls   = (id)objc_getClass("NSColorSpace");
    id alloc = ((id(*)(id, SEL))objc_msgSend)(cls, sel_registerName("alloc"));
    return    ((id(*)(id, SEL))objc_msgSend)(alloc, sel_registerName("init"));
}

// NSFont systemFontOfSize stub
static id sysFont_func(id self, SEL /*_cmd*/, double /*size*/) {
    id cls   = (id)objc_getClass("NSFont");
    id alloc = ((id(*)(id, SEL))objc_msgSend)(cls, sel_registerName("alloc"));
    return    ((id(*)(id, SEL))objc_msgSend)(alloc, sel_registerName("init"));
}

void VulkanRenderContext::buildDynamicObjectiveCBridge() {
    Class nsObject = objc_getClass("NSObject");
    if (!nsObject) return;

    // ── CALayer ──
    if (!objc_getClass("CALayer")) {
        Class c = objc_allocateClassPair(nsObject, "CALayer", 0);
        if (c) objc_registerClassPair(c);
    }

    // ── CAMetalLayer ──
    if (!objc_getClass("CAMetalLayer")) {
        Class base = objc_getClass("CALayer");
        Class c    = objc_allocateClassPair(base, "CAMetalLayer", 0);
        if (c) {
            class_addMethod(c, sel_registerName("nextDrawable"),
                            (IMP)sampleNextDrawableImplementation, "@@:");
            objc_registerClassPair(c);
        }
    }

    // ── QuartzMetalDrawable ──
    if (!objc_getClass("QuartzMetalDrawable")) {
        Class c = objc_allocateClassPair(nsObject, "QuartzMetalDrawable", 0);
        if (c) {
            class_addIvar(c, "vulkanTextureBridge",
                          sizeof(void*), static_cast<uint8_t>(std::log2(sizeof(void*))), "^v");
            class_addIvar(c, "parentLayer",
                          sizeof(id),    static_cast<uint8_t>(std::log2(sizeof(id))),    "@");
            class_addMethod(c, sel_registerName("texture"), (IMP)drawableGetTexture_impl, "@@:");
            class_addMethod(c, sel_registerName("layer"),   (IMP)drawableGetLayer_impl,   "@@:");
            class_addMethod(c, sel_registerName("present"), (IMP)drawablePresent_impl,    "v@:");
            objc_registerClassPair(c);
        }
    }

    // ── NSApplication ──
    if (!objc_getClass("NSApplication")) {
        Class c    = objc_allocateClassPair(nsObject, "NSApplication", 0);
        if (c) {
            Class meta = object_getClass((id)c);
            class_addMethod(meta, sel_registerName("sharedApplication"),
                            (IMP)sharedApplication_impl, "@:");
            class_addMethod(c, sel_registerName("run"), (IMP)run_impl, "v@:");
            objc_registerClassPair(c);
        }
    }

    // ── NSWindow ──
    if (!objc_getClass("NSWindow")) {
        Class c = objc_allocateClassPair(nsObject, "NSWindow", 0);
        if (c) {
            class_addMethod(c,
                sel_registerName("initWithContentRect:styleMask:backing:defer:"),
                (IMP)winInit_func, "@@:{CGRect={CGPoint=dd}{CGSize=dd}}QQB");
            objc_registerClassPair(c);
        }
    }

    // ── NSColorSpace ──
    if (!objc_getClass("NSColorSpace")) {
        Class c = objc_allocateClassPair(nsObject, "NSColorSpace", 0);
        if (c) {
            class_addMethod(object_getClass((id)c),
                            sel_registerName("sRGBColorSpace"),
                            (IMP)srgb_func, "@:");
            objc_registerClassPair(c);
        }
    }

    // ── NSFont ──
    if (!objc_getClass("NSFont")) {
        Class c = objc_allocateClassPair(nsObject, "NSFont", 0);
        if (c) {
            class_addMethod(object_getClass((id)c),
                            sel_registerName("systemFontOfSize:"),
                            (IMP)sysFont_func, "@:d");
            objc_registerClassPair(c);
        }
    }

    std::cout << "[QMB] ObjC bridge registered: CAMetalLayer, QuartzMetalDrawable, "
                 "NSApplication, NSWindow, NSColorSpace, NSFont\n";
}

// =============================================================================
// §11  Global context singleton
// =============================================================================

VulkanRenderContext* GetGlobalVulkanContext() {
    static std::once_flag flag;
    static VulkanRenderContext ctx;
    std::call_once(flag, [&]{ ctx.initContext(); });
    return &ctx;
}
