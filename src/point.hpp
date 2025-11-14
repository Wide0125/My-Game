#ifndef POINT_HPP
#define POINT_HPP

#include "Vector2.hpp"
struct Point {
    double x {};
    double y {};
};

inline constexpr Point& operator+=(Point& point, raylib::Vector2 vector) {
    point.x += vector.x;
    point.y += vector.y;
    return point;
}

#endif
