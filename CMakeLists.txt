cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)

project(MyGame)

include(cmake/CPM.cmake)

CPMAddPackage("gh:raysan5/raylib#5.5")

# find ALL .cpp source files in your project
# set the SOURCES variable to everything cmake finds
# configure depends means cmake will need to refresh your configuration every time you add a source file, otherwise that new file wont be built
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
# declare our app
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
  "raylib-cpp/include"
)
target_include_directories(${PROJECT_NAME} PRIVATE
  "${raylib_SOURCE_DIR}/src" # tell the compiler to look for headers in raylib's src directory, so you can include raylib.h properly 
)
target_link_libraries(${PROJECT_NAME} raylib) # link raylib's built static library

# target_compile_options(${PROJECT_NAME} PRIVATE
#     "-Wall"
#     "-Weffc++" "-Wextra" "-Wconversion"
#     "-Wsign-conversion" "-Werror"
# )
