#include <cmath>
#include <numbers>

class Degree {
    public:
        Degree(double degree)
            : m_angle {degree}
        {
            m_angle = std::fmod(m_angle, 360);
            if (m_angle < 0) {m_angle += 360;}
        }

        void add(double amount) {
            m_angle += amount;
            m_angle = std::fmod(m_angle, 360);
            if (m_angle < 0) {m_angle += 360;}
        }

        constexpr Degree plus(double amount) const {
            double ret {m_angle + amount};
            ret = std::fmod(m_angle, 360);
            if (ret < 0) {ret += 360;}
            return Degree{ret};
        }

        void set(double angle) {
            angle = std::fmod(angle, 360);
            if (angle < 0) {angle += 360;}
            m_angle = angle;
        }

        constexpr double get() const {
            return m_angle;
        }

        constexpr double radians() const {
            return m_angle * std::numbers::pi/180;
        }

        friend inline constexpr auto operator*(const Degree&, const auto);
        friend inline constexpr auto operator*(const auto, const Degree&);
        
        friend inline constexpr auto operator+(const Degree&, const Degree&);
        friend inline constexpr auto operator-(const Degree&, const Degree&);

        friend inline constexpr auto operator<=(const Degree&, const Degree&);

    private:
        double m_angle {0};
};

inline constexpr auto operator*(const Degree& degree, const auto multiple) {return degree.m_angle * multiple;}
inline constexpr auto operator*(const auto multiple, const Degree& degree) {return degree.m_angle * multiple;}

inline constexpr auto operator+(const Degree& degree1, const Degree& degree2) {return degree1.plus(degree2.m_angle);}
inline constexpr auto operator-(const Degree& degree1, const Degree& degree2) {return degree1.plus(-degree2.m_angle);}

inline constexpr Degree& operator+=(Degree& degree1, const Degree& degree2) {
    degree1.add(degree2.get());
    return degree1;
}

inline constexpr auto operator<=(const Degree& degree1, const Degree& degree2) {
    return degree1.m_angle <= degree2.m_angle;
}
