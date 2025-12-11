#include "player.hpp"
#include "Vector2.hpp"
#include "raylib-cpp.hpp"
#include "constants.hpp"
#include "degree.hpp"
#include <cmath>
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
            if (m_moveVelocity.y > 0) {m_moveVelocity.y = 0;}
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

void Player::attack(Player& playerOther) {

    if (static_cast<int>(std::floor(Vector2DotProduct(m_moveVelocity, m_moveVelocity)) == 0)) {return;}

    double distance {sqrt(Vector2DotProduct(m_position - playerOther.m_position, m_position - playerOther.m_position)) - m_size - playerOther.m_size};
    Degree playerAngle {atan(m_moveVelocity.y / m_moveVelocity.x) * 180/std::numbers::pi};
    Degree playerOtherAngle {atan(playerOther.m_position.y / playerOther.m_position.x) * 180/std::numbers::pi};

    DrawCircleSector(m_position, m_size + m_attackRange, playerAngle.get() + 60, playerAngle.get() - 60, 100, raylib::GREEN);

    DrawCircleSector(m_position, m_size, playerAngle.get() + 60, playerAngle.get() - 60, 100, m_color);

    if (playerOtherAngle <= playerAngle + 60 and playerOtherAngle >= playerAngle - 60 and distance <= m_attackRange) {
        
    }
}

void checkCollision(Player& player1, Player& player2) {

    Vector2 p1To2 {player2.m_position - player1.m_position};
    if (sqrt(Vector2DotProduct(p1To2, p1To2)) > player1.m_size + player2.m_size) {return;}

    Degree p1theta {acos(Vector2DotProduct(player1.m_moveVelocity, p1To2) / (sqrt(Vector2DotProduct(player1.m_moveVelocity, player1.m_moveVelocity)) * sqrt(Vector2DotProduct(p1To2, p1To2)))) * 180/std::numbers::pi};

    if (static_cast<int>(std::floor(p1theta.get())) == 0) {
        player1.setMoveVelocity({0, 0});
    }
    else if (p1theta < 90 or p1theta > 270) {
        Degree newVectorTheta {atan(p1To2.y / p1To2.x) - 90};
        Vector2 newVector = {static_cast<float>(sqrt(Vector2DotProduct(player1.m_moveVelocity, player1.m_moveVelocity)) * sin(p1theta.radians()) * cos(newVectorTheta.radians())), static_cast<float>(sqrt(Vector2DotProduct(player1.m_moveVelocity, player1.m_moveVelocity)) * sin(p1theta.radians()) * sin(newVectorTheta.radians()))};

        player1.m_moveVelocity = newVector;
    }

    Vector2 p2To1 {player1.m_position - player2.m_position};
    Degree p2theta {acos(Vector2DotProduct(player2.m_moveVelocity, p2To1) / (sqrt(Vector2DotProduct(player2.m_moveVelocity, player2.m_moveVelocity)) * sqrt(Vector2DotProduct(p2To1, p2To1)))) * 180/std::numbers::pi};

    if (static_cast<int>(std::floor(p2theta.get())) == 0) {
        player2.setMoveVelocity({0, 0});
    }
    else if (p2theta <= 90 or p2theta >= 270) {
        Degree newVectorTheta {atan(p2To1.y / p2To1.x) - 90};
        Vector2 newVector = {static_cast<float>(sqrt(Vector2DotProduct(player2.m_moveVelocity, player2.m_moveVelocity)) * sin(p2theta.radians()) * cos(newVectorTheta.radians())), static_cast<float>(sqrt(Vector2DotProduct(player2.m_moveVelocity, player2.m_moveVelocity)) * sin(p2theta.radians()) * sin(newVectorTheta.radians()))};

        player2.m_moveVelocity = newVector;
    }

};
