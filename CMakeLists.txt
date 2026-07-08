cmake_minimum_required(VERSION 3.30)

project(MyGame)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add option to enable/disable C++ 20 module
option(ENABLE_CPP20_MODULE "Enable C++ 20 module support for Vulkan" OFF)

# Enable C++ module dependency scanning only if C++ 20 module is enabled
if(ENABLE_CPP20_MODULE)
  set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(WIN32)
  set(glfw3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/glfw3.4/lib/cmake")
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/glfw3.4/lib/cmake")
endif()

find_package(Vulkan 1.4.350 REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(fastgltf REQUIRED)
find_package(vma REQUIRED)
find_package(KTX REQUIRED)

if(ENABLE_CPP20_MODULE)
  add_library(VulkanCppModule)
  add_library(Vulkan::cppm ALIAS VulkanCppModule)

  target_compile_definitions(VulkanCppModule PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1)
  target_include_directories(VulkanCppModule PUBLIC "${Vulkan_INCLUDE_DIR}")
  target_link_libraries(VulkanCppModule PUBLIC Vulkan::Vulkan)

  set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 20)

  # Add MSVC-specific compiler options for proper C++ module support
  if(MSVC)
    target_compile_options(VulkanCppModule PRIVATE
      /std:c++latest      # Use latest C++ standard for better module support
      /permissive-        # Standards conformance mode
      /Zc:__cplusplus     # Enable correct __cplusplus macro
      /EHsc               # Enable C++ exception handling
      /Zc:preprocessor    # Use conforming preprocessor
      /translateInclude   # Automatically translate #include to import for standard library
    )
  endif()

  target_sources(VulkanCppModule
    PUBLIC
    FILE_SET cxx_modules TYPE CXX_MODULES
    BASE_DIRS
    "${Vulkan_INCLUDE_DIR}"
    FILES
    "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
  )


  # Add the vulkan.cppm file directly as a source file
  target_sources(VulkanCppModule
    PRIVATE
    "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
  )
else()
  # Create a dummy interface library when C++ 20 module is disabled
  add_library(VulkanCppModule INTERFACE)
  add_library(Vulkan::cppm ALIAS VulkanCppModule)
  target_link_libraries(VulkanCppModule INTERFACE Vulkan::Vulkan)
  target_compile_definitions(VulkanCppModule
    INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
  )
endif()


add_executable(${PROJECT_NAME} 
  src/main.cpp
  src/VulkanInterfaceInit.cpp
  src/VulkanInterfaceLoadScene.cpp
  src/VulkanInterfaceHelpers.cpp
  src/VulkanInterfaceDraw.cpp
  )

target_link_libraries (${PROJECT_NAME} Vulkan::cppm glfw glm::glm fastgltf VulkanMemoryAllocator KTX::ktx)

find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
# Slang shader
function (add_slang_shader_target TARGET)
  cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
  set (ENTRY_POINTS -entry vertMain -entry fragMain)
  add_custom_command (
          OUTPUT ${SHADERS_DIR}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )
  add_custom_command (
          OUTPUT  ${SHADERS_DIR}/slang.spv
          COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
          WORKING_DIRECTORY ${SHADERS_DIR}
          DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
          COMMENT "Compiling Slang Shaders"
          VERBATIM
  )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()

set(SHADER_SLANG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/shader.slang)
add_slang_shader_target(foo SOURCES ${SHADER_SLANG_SOURCES})
add_dependencies(${PROJECT_NAME} foo)

target_compile_definitions(${PROJECT_NAME} PRIVATE 
  SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  SCENE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/scenes"
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    <vulkan/vulkan.hpp>
    <vulkan/vulkan_raii.hpp>
)

