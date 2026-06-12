cmake_minimum_required(VERSION 3.19)
project(QuartzMetalVulkan LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32)
    find_package(Vulkan REQUIRED)
else()
    if(DEFINED ENV{VULKAN_SDK})
        set(Vulkan_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(Vulkan_LIBRARY "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib")
        else()
            set(Vulkan_LIBRARY "$ENV{VULKAN_SDK}/Lib32/vulkan-1.lib")
        endif()
    else()
        message(FATAL_ERROR "VULKAN_SDK environment variable not set. Framework compilation aborted.")
    endif()
    add_library(Vulkan::Vulkan INTERFACE IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIR}" INTERFACE_LINK_LIBRARIES "${Vulkan_LIBRARY}")
    set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIR})
endif()
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/Metal.framework/Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/QuartzCore.framework/Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppServices.framework/Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppKit.framework/Headers
    ${VULKAN_INCLUDE_DIRS}
)
if(WIN32 AND NOT APPLE)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
file(GLOB_RECURSE BACKEND_SOURCES "Backend/src/*.cpp" "Backend/src/*.c" "Backend/src/*.cc")
add_library(QuartzMetalBackend SHARED ${BACKEND_SOURCES})
target_link_libraries(QuartzMetalBackend PUBLIC Vulkan::Vulkan)
if(NOT WIN32)
    target_link_libraries(QuartzMetalBackend PUBLIC objc)
endif()
add_library(Metal INTERFACE)
target_link_libraries(Metal INTERFACE QuartzMetalBackend)
add_library(QuartzCore INTERFACE)
target_link_libraries(QuartzCore INTERFACE QuartzMetalBackend Metal)
add_library(AppServices INTERFACE)
target_link_libraries(AppServices INTERFACE QuartzMetalBackend)
add_library(AppKit INTERFACE)
target_link_libraries(AppKit INTERFACE QuartzMetalBackend Metal QuartzCore AppServices)
file(GLOB_RECURSE DEMO_SOURCES "Demos/*.m" "Demos/*.mm" "Demos/*.cpp" "Demos/*.c")
if(DEMO_SOURCES)
    add_executable(DemoApp ${DEMO_SOURCES})
    target_link_libraries(DemoApp PRIVATE QuartzMetalBackend)
endif()
add_custom_command(TARGET QuartzMetalBackend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:QuartzMetalBackend>/Metal.framework/Headers
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/Metal.framework/Headers/Metal.h $<TARGET_FILE_DIR:QuartzMetalBackend>/Metal.framework/Headers/Metal.h
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/Metal.framework/Info.plist $<TARGET_FILE_DIR:QuartzMetalBackend>/Metal.framework/Info.plist
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:QuartzMetalBackend>/QuartzCore.framework/Headers
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/QuartzCore.framework/Headers/QuartzCore.h $<TARGET_FILE_DIR:QuartzMetalBackend>/QuartzCore.framework/Headers/QuartzCore.h
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/QuartzCore.framework/Info.plist $<TARGET_FILE_DIR:QuartzMetalBackend>/QuartzCore.framework/Info.plist
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:QuartzMetalBackend>/AppServices.framework/Headers
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppServices.framework/Headers/AppServices.h $<TARGET_FILE_DIR:QuartzMetalBackend>/AppServices.framework/Headers/AppServices.h
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppServices.framework/Info.plist $<TARGET_FILE_DIR:QuartzMetalBackend>/AppServices.framework/Info.plist
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:QuartzMetalBackend>/AppKit.framework/Headers
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppKit.framework/Headers/AppKit.h $<TARGET_FILE_DIR:QuartzMetalBackend>/AppKit.framework/Headers/AppKit.h
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/AppKit.framework/Info.plist $<TARGET_FILE_DIR:QuartzMetalBackend>/AppKit.framework/Info.plist
)
