#include "MapMath.h"
#include <cmath>
#include <algorithm>

namespace MapMath {

static constexpr double INCHES_PER_METRE = 39.3701;
static constexpr double PI = 3.14159265358979323846;

// --- Coordinate conversion ---
// Validated against Tyrian Codex: continent [43728, 28590] -> world (-419.5, _, 440.2)

void ContinentToWorldXZ(double cx, double cy,
                         const double contRect[4], const double mapRect[4],
                         float& outX, float& outZ)
{
    double cl = contRect[0], ct = contRect[1], cr = contRect[2], cb = contRect[3];
    double mMinX = mapRect[0], mMinY = mapRect[1], mMaxX = mapRect[2], mMaxY = mapRect[3];

    double fracX = (cr != cl) ? (cx - cl) / (cr - cl) : 0.0;
    double fracY = (cb != ct) ? (cy - ct) / (cb - ct) : 0.0;

    double mx = mMinX + fracX * (mMaxX - mMinX);
    double my = mMaxY - fracY * (mMaxY - mMinY);   // Y-flip: continent south+ -> map north+

    outX = static_cast<float>(mx / INCHES_PER_METRE);
    outZ = static_cast<float>(my / INCHES_PER_METRE);
}

void WorldXZToContinent(float worldX, float worldZ,
                         const double contRect[4], const double mapRect[4],
                         double& outCX, double& outCY)
{
    double cl = contRect[0], ct = contRect[1], cr = contRect[2], cb = contRect[3];
    double mMinX = mapRect[0], mMinY = mapRect[1], mMaxX = mapRect[2], mMaxY = mapRect[3];

    double mx = worldX * INCHES_PER_METRE;
    double my = worldZ * INCHES_PER_METRE;

    double fracX = (mMaxX != mMinX) ? (mx - mMinX) / (mMaxX - mMinX) : 0.0;
    double fracY = (mMaxY != mMinY) ? (mMaxY - my) / (mMaxY - mMinY) : 0.0;

    outCX = cl + fracX * (cr - cl);
    outCY = ct + fracY * (cb - ct);
}

double ContinentDistanceMetres(double x1, double y1, double x2, double y2,
                                const double contRect[4], const double mapRect[4])
{
    float wx1, wz1, wx2, wz2;
    ContinentToWorldXZ(x1, y1, contRect, mapRect, wx1, wz1);
    ContinentToWorldXZ(x2, y2, contRect, mapRect, wx2, wz2);
    double dx = wx2 - wx1;
    double dz = wz2 - wz1;
    return std::sqrt(dx * dx + dz * dz);
}

// --- Direction ---
// Continent: +X = east, +Y = south.
// In-game north = continent -Y. Standard compass: 0 = north, clockwise.

double Bearing(double fromCX, double fromCY, double toCX, double toCY)
{
    double dx = toCX - fromCX;     // + = east
    double dy = toCY - fromCY;     // + = south (continent)
    double rad = std::atan2(dx, -dy);   // atan2(east, north) -> 0=north, CW
    if (rad < 0.0) rad += 2.0 * PI;
    return rad;
}

const char* CompassDirection8(double bearingRad)
{
    double deg = std::fmod(bearingRad * 180.0 / PI, 360.0);
    if (deg < 0.0) deg += 360.0;

    if (deg < 22.5  || deg >= 337.5) return "K";
    if (deg < 67.5)                  return "KD";
    if (deg < 112.5)                 return "D";
    if (deg < 157.5)                 return "GD";
    if (deg < 202.5)                 return "G";
    if (deg < 247.5)                 return "GB";
    if (deg < 292.5)                 return "B";
    return "KB";
}

const char* DirectionArrow(double relativeBearingRad)
{
    double deg = std::fmod(relativeBearingRad * 180.0 / PI, 360.0);
    if (deg < 0.0) deg += 360.0;

    if (deg < 22.5  || deg >= 337.5) return "\xe2\x86\x91";   // up
    if (deg < 67.5)                  return "\xe2\x86\x97";   // up-right
    if (deg < 112.5)                 return "\xe2\x86\x92";   // right
    if (deg < 157.5)                 return "\xe2\x86\x98";   // down-right
    if (deg < 202.5)                 return "\xe2\x86\x93";   // down
    if (deg < 247.5)                 return "\xe2\x86\x99";   // down-left
    if (deg < 292.5)                 return "\xe2\x86\x90";   // left
    return "\xe2\x86\x96";                                     // up-left
}

// --- 3D Projection ---

static float Dot3(float ax, float ay, float az, float bx, float by, float bz)
{
    return ax * bx + ay * by + az * bz;
}

static float Len3(float x, float y, float z)
{
    return std::sqrt(x * x + y * y + z * z);
}

Mat4 BuildViewProj(Vec3 camPos, Vec3 camFront, Vec3 camTop,
                   float fovRad, float screenW, float screenH)
{
    if (fovRad < 0.01f) fovRad = DEFAULT_FOV;

    float fl = Len3(camFront.x, camFront.y, camFront.z);
    float fx = camFront.x, fy = camFront.y, fz = camFront.z;
    if (fl > 0.001f) { fx /= fl; fy /= fl; fz /= fl; }

    float tl = Len3(camTop.x, camTop.y, camTop.z);
    float ux, uy, uz;
    if (tl > 0.1f) { ux = camTop.x / tl; uy = camTop.y / tl; uz = camTop.z / tl; }
    else { ux = 0.f; uy = 1.f; uz = 0.f; }

    // LEFT-HANDED: right = up x front
    float rx = uy * fz - uz * fy;
    float ry = uz * fx - ux * fz;
    float rz = ux * fy - uy * fx;
    float rl = Len3(rx, ry, rz);
    if (rl > 0.001f) { rx /= rl; ry /= rl; rz /= rl; }

    // Recompute up = front x right
    ux = fy * rz - fz * ry;
    uy = fz * rx - fx * rz;
    uz = fx * ry - fy * rx;

    // View matrix (column-major: m[col][row])
    Mat4 view{};
    view.m[0][0] = rx;  view.m[1][0] = ry;  view.m[2][0] = rz;
    view.m[3][0] = -Dot3(rx, ry, rz, camPos.x, camPos.y, camPos.z);
    view.m[0][1] = ux;  view.m[1][1] = uy;  view.m[2][1] = uz;
    view.m[3][1] = -Dot3(ux, uy, uz, camPos.x, camPos.y, camPos.z);
    view.m[0][2] = fx;  view.m[1][2] = fy;  view.m[2][2] = fz;   // +f in row 3 (left-handed)
    view.m[3][2] = -Dot3(fx, fy, fz, camPos.x, camPos.y, camPos.z);
    view.m[3][3] = 1.f;

    // Perspective projection
    float aspect = (screenH > 0.f) ? screenW / screenH : 1.7778f;
    float t = std::tan(fovRad * 0.5f);

    Mat4 proj{};
    proj.m[0][0] = 1.f / (aspect * t);
    proj.m[1][1] = 1.f / t;
    proj.m[2][2] = FAR_CLIP / (FAR_CLIP - NEAR_CLIP);
    proj.m[2][3] = 1.f;    // w_clip = +z_view (left-handed)
    proj.m[3][2] = -(NEAR_CLIP * FAR_CLIP) / (FAR_CLIP - NEAR_CLIP);

    // viewProj = proj * view
    Mat4 vp{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                vp.m[c][r] += proj.m[k][r] * view.m[c][k];

    return vp;
}

bool WorldToScreen(Vec3 worldPos, const Mat4& vp,
                   float screenW, float screenH,
                   float& outX, float& outY, float& depth)
{
    float x = worldPos.x, y = worldPos.y, z = worldPos.z;
    float cx = vp.m[0][0]*x + vp.m[1][0]*y + vp.m[2][0]*z + vp.m[3][0];
    float cy = vp.m[0][1]*x + vp.m[1][1]*y + vp.m[2][1]*z + vp.m[3][1];
    float cz = vp.m[0][2]*x + vp.m[1][2]*y + vp.m[2][2]*z + vp.m[3][2];
    float cw = vp.m[0][3]*x + vp.m[1][3]*y + vp.m[2][3]*z + vp.m[3][3];

    if (cw <= 0.f) return false;

    float ndcX = cx / cw;
    float ndcY = cy / cw;

    if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f)
        return false;

    outX = ( ndcX + 1.f) * 0.5f * screenW;
    outY = (-ndcY + 1.f) * 0.5f * screenH;     // flip Y for screen top-left origin
    depth = cz / cw;
    return true;
}

float PerspectiveSize(float worldSize, float dist, float fov, float screenH)
{
    if (dist < 0.1f) dist = 0.1f;
    if (fov < 0.01f) fov = DEFAULT_FOV;
    float pxScale = (screenH * 0.5f) / std::tan(fov * 0.5f);
    float px = worldSize * pxScale / dist;
    return std::clamp(px, 18.f, 110.f);
}

// --- 2D Map overlay ---

bool ContinentToMapScreen(double cx, double cy,
                           double centerX, double centerY, double scale,
                           float screenW, float screenH,
                           float& outX, float& outY)
{
    if (scale <= 0.0) return false;
    double dx = cx - centerX;
    double dy = cy - centerY;
    double sx = dx / scale;
    double sy = dy / scale;
    outX = static_cast<float>(sx) + screenW * 0.5f;
    outY = static_cast<float>(sy) + screenH * 0.5f;
    return outX >= -50.f && outX <= screenW + 50.f &&
           outY >= -50.f && outY <= screenH + 50.f;
}

} // namespace MapMath
