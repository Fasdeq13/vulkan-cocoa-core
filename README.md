# vulkan-cocoa-core (QMV Core Engine)

**vulkan-cocoa-core** is a high-performance, hardware-independent graphics pipeline bridge designed to translate and redirect Apple's Cocoa, CoreAnimation, and Metal 3 interfaces directly into native Vulkan 1.3 API calls at the Darwin/XNU kernel level, replacing the proprietary Apple WindowServer on standard PC hardware.

## Key Technological Systems

*   **Hardware-Independent Swapchain:** Dynamically manages video memory, allocating buffers in `DEVICE_LOCAL` VRAM and using dedicated Vulkan queues for smooth 4K rendering.
*   **Multi-Process Texture Sharing (IOSurface API):** Uses POSIX shared memory (`shm_open`, `mmap`) and Vulkan external handles (`VK_KHR_external_memory_fd`) to enable zero-stall texture streaming between sandboxed browser processes and the GPU compositor.
*   **Kqueue/Mach Native Event Loop:** An asynchronous dispatcher monitoring kernel ports and timers to translate input signals into `NSEvent` structures via the Objective-C runtime for full UI interactivity.
*   **In-Memory JIT Shader Compiler:** An LLVM-based translator that intercepts Apple AIR bytecode, converts it to SPIR-V, injects `NonUniformEXT` for bindless support, and converts memory barriers to `ControlBarrier`.
