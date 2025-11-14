#include <raylib-cpp.hpp>
#include <raylib.h>
#include <string>

static constexpr int FPS {60};
static constexpr double GRAVITYACCEL {9.807 * 3}; // in pixels/second^2
static std::vector<raylib::Rectangle> PLATFORMS {{100, 350, 600, 30}};

class Player {
    public:
        void drawPlayer() const {
            DrawCircle(m_position.x, m_position.y, m_size, m_color);
        }
        void movePlayer() {
            m_position += m_moveVelocity / FPS;
            m_position.y += m_fallVelocity / FPS;
        }
        void fallPlayer() {
            for(auto a: PLATFORMS) {
                if(checkBelowCollision(a)) {
                    m_fallVelocity = 0;
                    m_position.y = a.GetY() - m_size;
                    m_jumpsRemaining = m_jumps;
                    return;
                }
            }
            m_fallVelocity += GRAVITYACCEL;
        }
        void setMoveVelocity(const Vector2& velocity) {
            m_moveVelocity = velocity;
        }
        void setMoveVelocityX(double x) {
            m_moveVelocity.x = x;
        }
        void setMoveVelocityY(double y) {
            m_moveVelocity.y = y;
        }

        bool checkBelowCollision(raylib::Rectangle platform) const {
            return (m_position.x + m_size >= platform.x and m_position.x - m_size <= platform.x + platform.GetWidth() and m_position.y + m_size >= platform.y);
        }

        void jump() {
            if (checkBelowCollision(PLATFORMS[0])) {
                m_fallVelocity = -m_jumpPower;
                m_jumpsRemaining -= 1;
            }
            else if (m_jumpsRemaining > 0) {
                m_fallVelocity = -m_jumpPower;
                m_jumpsRemaining -= 1;
            }
        }

    private:
        raylib::Vector2 m_position {100, 100};
        double m_size {20};
        raylib::Vector2 m_moveVelocity {0, 0}; // in pixels/second
        double m_fallVelocity {0.0}; // in pixels/second
        raylib::Color m_color {RED};
        double m_jumpPower {500};
        int m_jumps {2};
        int m_jumpsRemaining {m_jumps};
};

int main() {
    const int screenWidth = 800;
    const int screenHeight = 450;

    raylib::InitWindow(screenWidth, screenHeight, "raylib [core] example - basic window");

    Player player1 {};

    SetTargetFPS(FPS);
    while (!WindowShouldClose())
    {
        player1.setMoveVelocity({0, 0});
        if (IsKeyPressed(KEY_W)) {player1.jump();}
        // if (IsKeyDown(KEY_S)) {player1.setMoveVelocityY(100);}
        if (IsKeyDown(KEY_D)) {player1.setMoveVelocityX(300);}
        if (IsKeyDown(KEY_A)) {player1.setMoveVelocityX(-300);}

        BeginDrawing();
        
            ClearBackground(RAYWHITE);

            player1.movePlayer();
            player1.fallPlayer();
            player1.drawPlayer();
            DrawRectangleRec(PLATFORMS[0], LIGHTGRAY);
            raylib::DrawText(std::to_string(GetFPS()), 10, 10, 20, RED);

        EndDrawing();
    }
    CloseWindow();
    return 0;
}
