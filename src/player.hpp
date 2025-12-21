#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "Vector2.hpp"
#include "raylib-cpp.hpp"

class Player {
    public:
        Player(raylib::Vector2 position, double size = 20, raylib::Color color = BLACK, int healthbarLocation = 0, bool leftbar = true)
            : m_position {position}, m_size {size}, m_color {color}, m_healthbarLocation {healthbarLocation}, m_leftbar {leftbar}
        {}

        void draw() const;
        void move();
        void fall();
        void setMoveVelocity(const Vector2& velocity);
        void setMoveVelocityX(double x);
        void setMoveVelocityY(double Y);

        bool checkBelowCollision(raylib::Rectangle& platform) const;

        void jump();

        void attack(Player&);

        void block();

        void cooldown();

        double calculateDamage(const Player& playerOther) const;
        
        bool dead();

        friend void checkCollision(Player& player1, Player& player2);

    private:
        raylib::Vector2 m_position {0, 0};
        double m_size {20};
        raylib::Color m_color {RED};
        raylib::Vector2 m_moveVelocity {0, 0};
        double m_jumpPower {500};
        double m_attackRange {30};
        int m_jumps {2};
        int m_jumpsRemaining {m_jumps};
        double m_hitPoints {100};
        int m_healthbarLocation {0};
        bool m_leftbar {true};
        bool m_blocking {false};
        int m_blockLength {3};
        int m_blockCooldown {6};
        int m_blockCooldownRemaining {0};
};

#endif // !PLAYER_HPP


