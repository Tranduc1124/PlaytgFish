#pragma once

// Vector3/Quaternion lấy cảm hứng từ tphat/Helper/Vector3.h (bản rút gọn,
// tối ưu cho việc chiếu toạ độ & tính khoảng cách cho ESP).

#include <cmath>

struct Vector3 {
    union {
        struct { float x, y, z; };
        float data[3];
    };

    Vector3() : x(0), y(0), z(0) {}
    explicit Vector3(float v) : x(v), y(v), z(v) {}
    Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    static Vector3 zero() { return Vector3(0, 0, 0); }
    static Vector3 one() { return Vector3(1, 1, 1); }

    float magnitude() const { return std::sqrt(x * x + y * y + z * z); }
    float sqrMagnitude() const { return x * x + y * y + z * z; }

    Vector3 normalized() const {
        const float m = magnitude();
        if (m < 1e-6f) return zero();
        return Vector3(x / m, y / m, z / m);
    }

    static float dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    static Vector3 cross(const Vector3& a, const Vector3& b) {
        return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }

    static float distance(const Vector3& a, const Vector3& b) { return (a - b).magnitude(); }
    static float distanceSq(const Vector3& a, const Vector3& b) { return (a - b).sqrMagnitude(); }

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(float s) const { return Vector3(x / s, y / s, z / s); }
    Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    bool operator==(const Vector3& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct Quaternion {
    union {
        struct { float x, y, z, w; };
        float data[4];
    };
    Quaternion() : x(0), y(0), z(0), w(1) {}
    Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quaternion identity() { return Quaternion(0, 0, 0, 1); }

    float magnitude() const { return std::sqrt(x * x + y * y + z * z + w * w); }

    Quaternion normalized() const {
        const float m = magnitude();
        if (m < 1e-6f) return identity();
        return Quaternion(x / m, y / m, z / m, w / m);
    }
};
