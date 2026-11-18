// 3D vector used by the car physics (body and wheel positions, velocities, forces).
//
// All arithmetic is single precision, one operation per component, exactly like the original
// (SSE scalar float; the build disables fused multiply-add), so results match bit for bit.
#pragma once

struct Vec3 {
    float x, y, z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 &operator+=(const Vec3 &o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    Vec3 &operator-=(const Vec3 &o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    Vec3 &operator/=(float s) {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }
    Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
};

inline Vec3 operator*(float s, const Vec3 &v) { return Vec3(v.x * s, v.y * s, v.z * s); }
