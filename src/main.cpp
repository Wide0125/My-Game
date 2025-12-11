#include <print>
#include <raylib-cpp.hpp>
#include <raylib.h>
#include <string>
#include "degree.hpp"
#include "player.hpp"
#include "constants.hpp"

int main() {

    raylib::InitWindow(screenWidth, screenHeight);

    Player player1 {{200, 300}, 20, RED};
    Player player2 {{960 - 200, 300}, 20, BLUE};

    SetTargetFPS(FPS);
    while (!WindowShouldClose()) {
        player1.setMoveVelocityX(0);
        if (IsKeyPressed(KEY_W)) {player1.jump();}
        if (IsKeyDown(KEY_D)) {player1.setMoveVelocityX(300);}
        if (IsKeyDown(KEY_A)) {player1.setMoveVelocityX(-300);}

        player2.setMoveVelocityX(0);
        if (IsKeyPressed(KEY_UP)) {player2.jump();}
        if (IsKeyDown(KEY_RIGHT)) {player2.setMoveVelocityX(300);}
        if (IsKeyDown(KEY_LEFT)) {player2.setMoveVelocityX(-300);}

        BeginDrawing();
        
            ClearBackground(RAYWHITE);

            player1.fall();
            player2.fall();

            checkCollision(player1, player2);

            player1.move();
            player2.move();

            player1.draw();
            player2.draw();
            
            DrawRectangleRec(PLATFORMS[0], LIGHTGRAY);
            raylib::DrawText(std::to_string(GetFPS()), 10, 10, 20, RED);

            if (IsKeyPressed(KEY_F)) {
                player1.attack(player2);
            }
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
