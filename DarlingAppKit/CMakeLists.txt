cmake_minimum_required(VERSION 3.15)
project(DarlingAppKit LANGUAGES OBJC CXX)

set(CMAKE_OBJC_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

add_library(DarlingAppKit SHARED
    AppKitClasses.m
)

target_include_directories(DarlingAppKit PUBLIC
    .
    ../DarlingMetal/include
    ../DarlingQuartzCore
    ${Vulkan_INCLUDE_DIRS}
)

target_link_directories(DarlingAppKit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../DarlingMetal/build
    ${CMAKE_CURRENT_SOURCE_DIR}/../DarlingQuartzCore/build
)

target_link_libraries(DarlingAppKit PRIVATE
    DarlingMetal
    DarlingQuartzCore
    ${Vulkan_LIBRARIES}
)

if(APPLE)
    target_link_options(DarlingAppKit PRIVATE "-framework" "Foundation")
else()
    target_compile_options(DarlingAppKit PRIVATE "-fobjc-runtime=gnustep" "-fblocks")
    target_link_libraries(DarlingAppKit PRIVATE objc)
endif()
