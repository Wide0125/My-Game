#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "Vector2.hpp"
#include "raylib-cpp.hpp"

class Player {
    public:
        Player(raylib::Vector2 position, double size = 20, raylib::Color color = BLACK)
            : m_position {position}, m_size {size}, m_color {color}
        {}

        void draw() const;
        void move();
        void fall();
        void setMoveVelocity(const Vector2& velocity);
        void setMoveVelocityX(double x);
        void setMoveVelocityY(double Y);

        bool checkBelowCollision(raylib::Rectangle& platform) const;

        void jump();

        friend void checkCollision(Player& player1, Player& player2);

    private:
        raylib::Vector2 m_position {0, 0};
        double m_size {20};
        raylib::Color m_color {RED};
        raylib::Vector2 m_moveVelocity {0, 0};
        double m_jumpPower {500};
        int m_jumps {2};
        int m_jumpsRemaining {m_jumps};
};

#endif // !PLAYER_HPP


