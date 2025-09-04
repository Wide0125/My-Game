#include <raylib-cpp.hpp>

int main() {
    InitWindow(960, 540, "Hajun");
    raylib::Image hajunFace {"IMG_8745.PNG"};
    hajunFace.Resize(585/2, 1266/2);

    raylib::Texture2D hajunFaceTexture {hajunFace};

    while (!WindowShouldClose()) {
        BeginDrawing();
        hajunFaceTexture.Draw(100, 0, Color {RAYWHITE});
        EndDrawing();
    }
    return 0;
}
