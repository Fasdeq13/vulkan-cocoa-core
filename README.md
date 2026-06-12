# darling-vulkan-cocoa (QuartzMetal Pipeline)

**darling-vulkan-cocoa** is a high-performance hardware-accelerated graphics pipeline designed to implement and redirect Apple’s Cocoa and Metal 3 interfaces into native Vulkan API calls. 

The framework is highly optimized for Darwin-based operating systems and cross-platform POSIX environments, allowing software to leverage modern discrete GPU power (NVIDIA/AMD/Intel) at max refresh rates without interface micro-stutters.

### Project Structure & Components

* **`Frameworks/`** — Core implementation of high-level Apple system фреймворков (`AppKit`, `Metal`, `QuartzCore`, `AppServices`). It handles standard objective-C lifecycles, windows (`NSWindow`), and CoreAnimation layers (`CALayer`).
* **`Backend/`** — A low-level C++ execution engine that maps layout structures directly to physical Vulkan devices, managing dedicated VRAM heaps and handling on-the-fly JIT compilation (AIR to SPIR-V).
