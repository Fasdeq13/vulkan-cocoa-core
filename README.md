# vulkan-cocoa-core (QMV Core Engine)

**vulkan-cocoa-core** is a high-performance, hardware-independent graphics pipeline bridge designed to translate and redirect Apple’s Cocoa, CoreAnimation, and Metal 3 interfaces directly into native Vulkan 1.3 API calls at the Darwin/XNU kernel level.

The project is architected as a monolithic system backend that replaces the proprietary Apple WindowServer on standard PC hardware.
## Key Technological Systems

* **Hardware-Independent Swapchain:** A dynamic video memory manager that automatically handles discrete and integrated GPUs. It allocates rendering buffers directly into high-speed `DEVICE_LOCAL` VRAM and leverages dedicated Vulkan queues for graphics and asynchronous data transfers, ensuring smooth 4K video streams and stutter-free playback.
* **Multi-Process Texture Sharing (IOSurface API):** A native texture-sharing subsystem powered by POSIX shared memory (`shm_open` and `mmap`). It links memory allocation descriptors to Vulkan external handles (`VK_KHR_external_memory_fd`), allowing sandboxed browser renderer processes to stream layout textures directly to the primary GPU compositor process with zero system stalls.
* **Kqueue/Mach Native Event Loop:** An asynchronous input dispatcher that simultaneously monitors kernel Mach ports and high-resolution frame-rate timers. It extracts hardware mouse coordinates and keyboard signals from incoming kernel messages on the fly, packaging them into standard-compliant `NSEvent` structures via the Objective-C runtime to ensure full UI interactivity.
* **In-Memory JIT Shader Compiler:** An integrated LLVM-based metadata and MSL intrinsic translator. It intercepts Apple AIR binary bytecode, injects `NonUniformEXT` decorations on the fly to enable bindless Argument Buffers, converts proprietary Apple memory barriers into valid Vulkan control barriers (`ControlBarrier`), and generates clean SPIR-V binaries entirely in system memory.
