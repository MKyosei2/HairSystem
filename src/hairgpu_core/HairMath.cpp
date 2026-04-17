#include "hairgpu/Math.h"

namespace hairgpu
{
    Mat4 Identity()
    {
        return Mat4();
    }

    Mat4 PerspectiveFovRH(float fovYRadians, float aspectRatio, float nearZ, float farZ)
    {
        Mat4 result = {};
        const float yScale = 1.0f / std::tan(fovYRadians * 0.5f);
        const float xScale = yScale / aspectRatio;

        result.m[0] = xScale;
        result.m[5] = yScale;
        result.m[10] = farZ / (nearZ - farZ);
        result.m[11] = -1.0f;
        result.m[14] = (nearZ * farZ) / (nearZ - farZ);
        return result;
    }

    Mat4 LookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        const Vec3 forward = Normalize(eye - target);
        const Vec3 right = Normalize(Cross(up, forward));
        const Vec3 newUp = Cross(forward, right);

        Mat4 result = Identity();
        result.m[0] = right.x;
        result.m[1] = newUp.x;
        result.m[2] = forward.x;
        result.m[4] = right.y;
        result.m[5] = newUp.y;
        result.m[6] = forward.y;
        result.m[8] = right.z;
        result.m[9] = newUp.z;
        result.m[10] = forward.z;
        result.m[12] = -Dot(right, eye);
        result.m[13] = -Dot(newUp, eye);
        result.m[14] = -Dot(forward, eye);
        return result;
    }

    Mat4 Multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 result = {};
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                result.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                    a.m[row * 4 + 3] * b.m[3 * 4 + col];
            }
        }
        return result;
    }

    Mat4 RotationX(float radians)
    {
        Mat4 result = Identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        result.m[5] = c;
        result.m[6] = s;
        result.m[9] = -s;
        result.m[10] = c;
        return result;
    }

    Mat4 RotationY(float radians)
    {
        Mat4 result = Identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        result.m[0] = c;
        result.m[2] = -s;
        result.m[8] = s;
        result.m[10] = c;
        return result;
    }

    Mat4 Translation(const Vec3& offset)
    {
        Mat4 result = Identity();
        result.m[12] = offset.x;
        result.m[13] = offset.y;
        result.m[14] = offset.z;
        return result;
    }

    Vec3 TransformPoint(const Mat4& m, const Vec3& p)
    {
        return Vec3(
            p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12],
            p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13],
            p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10] + m.m[14]);
    }
}
