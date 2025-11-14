cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)

project(MyGame)

set(RAYLIB_VERSION 5.5)
find_package(raylib ${RAYLIB_VERSION} QUIET) # QUIET or REQUIRED
if (NOT raylib_FOUND) # If there's none, fetch and build raylib
  include(FetchContent)
  FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
  )
  FetchContent_GetProperties(raylib)
  if (NOT raylib_POPULATED) # Have we downloaded raylib yet?
    set(FETCHCONTENT_QUIET NO)
    FetchContent_MakeAvailable(raylib)
  endif()
endif()

# find ALL .cpp source files in your project
# set the SOURCES variable to everything cmake finds
# configure depends means cmake will need to refresh your configuration every time you add a source file, otherwise that new file wont be built
# declare our app
add_executable(${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE
  "raylib-cpp/include" 
)
target_link_libraries(${PROJECT_NAME} raylib)

target_compile_options(${PROJECT_NAME} PRIVATE
    "-Wpedantic-errors -Wall"
    "-Weffc++ -Wextra -Wconversion"
    "-Wsign-conversion -Werror"
)
