# darling-vulkan-cocoa

**darling-vulkan-cocoa** is a compact library designed to translate Apple’s Cocoa interfaces into Linux graphics using the Vulkan API. The project has a minimal layout and consists of just a few core components.

### Project Structure and Files

* **`Frameworks/`** — translates high-level Mac UI functions (AppKit/CoreAnimation) into abstract graphic layers and render commands.
* **`Backend/`** — translates these layers directly into native Vulkan API calls for hardware-accelerated rendering on Linux.
* **`CMakeLists.txt`** — translates the project's source code into buildable binaries and libraries using the CMake build system.
