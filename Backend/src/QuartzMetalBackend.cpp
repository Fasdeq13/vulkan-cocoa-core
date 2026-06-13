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
#include <sys/types.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_metal.h>
#include <LLVMSPIRVLib/LLVMSPIRVLib.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/MemoryBuffer.h>
#include "QuartzMetalBackend.h"
#include <objc/runtime.h>
#include <objc/message.h>

extern "C" id objc_msgSend(id self, SEL op, ...);

VulkanRenderContext* GetGlobalVulkanContext() {
    static VulkanRenderContext ctx;
    return &ctx;
}

VulkanRenderContext::VulkanRenderContext() : instance(VK_NULL_HANDLE), physicalDevice(VK_NULL_HANDLE), device(VK_NULL_HANDLE) {}

VulkanRenderContext::~VulkanRenderContext() {
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

void VulkanRenderContext::initContext() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());
    physicalDevice = devs[0];

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> props(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, props.data());

    uint32_t gIdx = 0xFFFFFFFF, tIdx = 0xFFFFFFFF;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gIdx = i;
        if (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) tIdx = i;
    }
    
    graphicsQueueFamilyIndex = (gIdx != 0xFFFFFFFF) ? gIdx : 0;
    transferQueueFamilyIndex = (tIdx != 0xFFFFFFFF) ? tIdx : graphicsQueueFamilyIndex;

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> qCIs;
    
    VkDeviceQueueCreateInfo gCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    gCI.queueFamilyIndex = graphicsQueueFamilyIndex;
    gCI.queueCount = 1;
    gCI.pQueuePriorities = &priority;
    qCIs.push_back(gCI);

    if (graphicsQueueFamilyIndex != transferQueueFamilyIndex) {
        VkDeviceQueueCreateInfo tCI{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        tCI.queueFamilyIndex = transferQueueFamilyIndex;
        tCI.queueCount = 1;
        tCI.pQueuePriorities = &priority;
        qCIs.push_back(tCI);
    }

    std::vector<const char*> ext = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME };
    VkDeviceCreateInfo dCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dCI.queueCreateInfoCount = static_cast<uint32_t>(qCIs.size());
    dCI.pQueueCreateInfos = qCIs.data();
    dCI.enabledExtensionCount = static_cast<uint32_t>(ext.size());
    dCI.ppEnabledExtensionNames = ext.data();

    vkCreateDevice(physicalDevice, &dCI, nullptr, &device);
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    vkGetDeviceQueue(device, transferQueueFamilyIndex, 0, &transferQueue);
}

VkDeviceMemory VulkanRenderContext::allocateVideoMemory(VkMemoryRequirements req, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t type = 0;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            type = i; break;
        }
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    VkDeviceMemory mem;
    vkAllocateMemory(device, &ai, nullptr, &mem);
    return mem;
}

bool AIRToSPIRVCompiler::compileAIRToSPIRV(const std::vector<uint8_t>& airBytecode, std::vector<uint32_t>& outSpirv) {
    if (airBytecode.empty()) return false;
    std::string bin(airBytecode.begin(), airBytecode.end());
    llvm::MemoryBufferRef buf(bin, "AIR_Stream");
    llvm::LLVMContext llvmCtx;
    auto modOrErr = llvm::parseBitcodeFile(buf, llvmCtx);
    if (!modOrErr) return false;
    std::unique_ptr<llvm::Module> M = std::move(modOrErr.get());
    
    SPIRV::TranslatorOpts opts;
    opts.enableAllExtensions();
    std::string errLog;
    std::ostringstream spirvStream(std::ios::binary);
    
    if (!llvm::writeSpirv(M.get(), opts, spirvStream, errLog)) return false;
    
    std::string spirvStr = spirvStream.str();
    size_t words = spirvStr.size() / sizeof(uint32_t);
    outSpirv.resize(words);
    std::memcpy(outSpirv.data(), spirvStr.data(), words * sizeof(uint32_t));
    return true;
}

static id sharedApplication_impl(id self, SEL _cmd) { return self; }
static void run_impl(id self, SEL _cmd) {}
static id winInit_func(id self, SEL _cmd, CGRect rect, uint64_t mask, uint64_t backing, bool defer) { return self; }
static id srgb_func(id self, SEL _cmd) { return self; }
static id sysFont_func(id self, SEL _cmd, double size) { return self; }

extern "C" {
    int NSApplicationMain(int argc, const char* argv[]) {
        Class nsObject = objc_getClass("NSObject");
        if (!objc_getClass("NSApplication")) {
            Class c = objc_allocateClassPair(nsObject, "NSApplication", 0);
            if (c) {
                Class meta = object_getClass((id)c);
                class_addMethod(meta, sel_registerName("sharedApplication"), (IMP)sharedApplication_impl, "@:");
                class_addMethod(c, sel_registerName("run"), (IMP)run_impl, "v@:");
                objc_registerClassPair(c);
            }
        }
        if (!objc_getClass("NSWindow")) {
            Class c = objc_allocateClassPair(nsObject, "NSWindow", 0);
            if (c) {
                class_addMethod(c, sel_registerName("initWithContentRect:styleMask:backing:defer:"), (IMP)winInit_func, "@@:{CGRect={CGPoint=dd}{CGSize=dd}}QQB");
                objc_registerClassPair(c);
            }
        }
        return 0;
    }
}
