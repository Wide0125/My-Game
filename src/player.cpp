#include "player.hpp"
#include "Vector2.hpp"
#include "raylib-cpp.hpp"
#include "constants.hpp"
#include "degree.hpp"
#include <raylib.h>
#include <raymath.h>

void Player::draw() const {
        DrawCircle(m_position.x, m_position.y, m_size, m_color);
    }

void Player::move() {
    m_position += m_moveVelocity / FPS;
}
void Player::fall() {
    for(auto a: PLATFORMS) {
        if(checkBelowCollision(a)) {
            m_moveVelocity.y = 0;
            m_position.y = a.GetY() - m_size;
            m_jumpsRemaining = m_jumps;
            return;
        }
    }
    m_moveVelocity.y += GRAVITYACCEL;
}
void Player::setMoveVelocityX(double x) {
    m_moveVelocity.x = x;
}
void Player::setMoveVelocityY(double y) {
    m_moveVelocity.y = y;
}

void Player::setMoveVelocity(const Vector2& velocity) {
    m_moveVelocity = velocity;
}

bool Player::checkBelowCollision(raylib::Rectangle& platform) const {
    return ((m_position.x + m_size >= platform.x
        and m_position.x - m_size <= platform.x + platform.GetWidth()))
        and m_position.y + m_size >= platform.y
        and m_position.y + m_size <= platform.y + platform.GetHeight() ;
}

void Player::jump() {
    if (checkBelowCollision(PLATFORMS[0])) {
        m_moveVelocity.y = -m_jumpPower;
        m_jumpsRemaining -= 1;
    }
    else if (m_jumpsRemaining > 0) {
        m_moveVelocity.y = -m_jumpPower;
        m_jumpsRemaining -= 1;
    }
}

void checkCollision(Player& player1, Player& player2) {
    if(sqrt(Vector2DotProduct(player1.m_position - player2.m_position, player1.m_position - player2.m_position)) <= player1.m_size + player2.m_size) {
        raylib::Vector2 p1to2 {player2.m_position - player1.m_position};

        Degree theta1 {atan(player1.m_moveVelocity.y/player1.m_moveVelocity.x) * 180/PI}; // player1 calculation
        Degree theta2 {atan(p1to2.y/p1to2.x) * 180/PI};

        if(theta1 - theta2 <= Degree{90}) {
            player1.m_moveVelocity.x = sqrt(Vector2DotProduct(player1.m_moveVelocity, player1.m_moveVelocity)) * sin((theta1 - theta2).radians()) * cos((theta1 - theta2).radians() - PI/2);
            player1.m_moveVelocity.y = sqrt(Vector2DotProduct(player1.m_moveVelocity, player1.m_moveVelocity)) * sin((theta1 - theta2).radians()) * sin((theta1 - theta2).radians() - PI/2);
        }
        
        raylib::Vector2 p2to1 {player1.m_position - player2.m_position};

        theta1 = atan(player2.m_moveVelocity.y/player2.m_moveVelocity.x) * 180/PI; // player2 calculation
        theta2 = atan(p2to1.y/p2to1.x) * 180/PI;

        if(theta1 - theta2 <= Degree{90}) {
            player2.m_moveVelocity.x = sqrt(Vector2DotProduct(player2.m_moveVelocity, player2.m_moveVelocity)) * sin((theta1 - theta2).radians()) * cos((theta1 - theta2).radians() - PI/2);
            player2.m_moveVelocity.y = sqrt(Vector2DotProduct(player2.m_moveVelocity, player2.m_moveVelocity)) * sin((theta1 - theta2).radians()) * sin((theta1 - theta2).radians() - PI/2);
        }
    }
};
