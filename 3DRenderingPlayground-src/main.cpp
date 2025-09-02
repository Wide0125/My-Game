#include "raylib-cpp.hpp"

// struct Vector3D {
//     double x {};
//     double y {};
//     double z {};
// };

class Vertex {
    public:
        Vertex(double x = 0.0, double y = 0.0, double z = 0.0)
            : m_x {x}, m_y {y}, m_z {z}
            {}
    private:
        double m_x {};
        double m_y {};
        double m_z {};
};

class Trimesh {
    public:
        Trimesh(Vertex first = {0.0, 0.0, 0.0},
                Vertex second = {0.0, 0.0, 0.0},
                Vertex third = {0.0, 0.0, 0.0})
            : m_first {first}, m_second {second}, m_third {third}
            {}
    private:
        Vertex m_first {}; // the first vertex will be used to express the mesh's position
        Vertex m_second {};
        Vertex m_third {};
};

int main() {
    constexpr double distanceFromZero {};
    Trimesh trimesh {{1, 2, distanceFromZero},
                    {2, 0, distanceFromZero},
                    {0, 0, distanceFromZero}};
    
    Vertex cameraPos {};

    raylib::InitWindow(960, 540, "3D Render Playground");
    
    return 0;
}

