#ifndef CONSTANTS_HPP
#define CONSTANTS_HPP

#include "raylib-cpp.hpp"

static constexpr int screenWidth = 960;
static constexpr int screenHeight = 540;

static constexpr int FPS {60};
static constexpr double GRAVITYACCEL {9.807 * 3}; // in pixels/second^2
static std::vector<raylib::Rectangle> PLATFORMS {{screenWidth/2 - 350, 350, 700, 30}};

#endif // !CONSTANTS_HPP

