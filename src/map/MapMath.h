#pragma once
#include <vector>
#include <string>

namespace MapMath {

struct Vec3 { float x, y, z; };
struct Mat4 { float m[4][4] = {}; };

// --- World marker: what the overlay renders ---
// Producers fill this in world metres. Entity marker does ContinentToWorldXZ;
// a future TacO producer puts world coords directly.
enum class MarkerKind { Point, Trail };

struct WorldMarker {
    Vec3 pos;                           // world metres (MumbleLink space)
    std::string label;
    int mapId = 0;
    MarkerKind kind = MarkerKind::Point;
    // Trail points live here when kind==Trail (Dilim 2).
    // Zero cost for Point markers (empty vector).
    std::vector<Vec3> trailPoints;
};

// --- Coordinate conversion ---
// contRect: [left, top, right, bottom] (continent, Y increases southward)
// mapRect:  [minX, minY, maxX, maxY]   (inches, Y increases northward)

void ContinentToWorldXZ(double cx, double cy,
                         const double contRect[4], const double mapRect[4],
                         float& outX, float& outZ);

void WorldXZToContinent(float worldX, float worldZ,
                         const double contRect[4], const double mapRect[4],
                         double& outCX, double& outCY);

double ContinentDistanceMetres(double x1, double y1, double x2, double y2,
                                const double contRect[4], const double mapRect[4]);

// --- Direction ---
// Bearing from (fromCX,fromCY) to (toCX,toCY) in continent coords.
// Returns radians, 0 = north, clockwise.
// Continent Y increases south, so north = decreasing Y.
double Bearing(double fromCX, double fromCY, double toCX, double toCY);

const char* CompassDirection8(double bearingRad);
const char* DirectionArrow(double relativeBearingRad);

// --- 3D Projection (LEFT-HANDED, GW2 convention) ---
// right = up x front, view row3 = +f, proj w_clip = +z_view.
// Uses the BuildViewProj from GW2-Nexus-Pathing / Tyrian Codex.

static constexpr float DEFAULT_FOV = 1.222f;   // ~70 degrees fallback
static constexpr float NEAR_CLIP = 0.5f;
static constexpr float FAR_CLIP  = 8000.f;

Mat4 BuildViewProj(Vec3 camPos, Vec3 camFront, Vec3 camTop,
                   float fovRad, float screenW, float screenH);

// Returns false if behind camera or outside frustum (tolerance 1.2x NDC).
bool WorldToScreen(Vec3 worldPos, const Mat4& viewProj,
                   float screenW, float screenH,
                   float& outX, float& outY, float& depth);

float PerspectiveSize(float worldSize, float dist, float fov, float screenH);

// --- 2D Map overlay (M-key open map, IsMapOpen=true) ---
// Compass.Center/Scale switch to world-map view when map is open.
// Rotation is skipped (world map is always north-up).
bool ContinentToMapScreen(double cx, double cy,
                           double centerX, double centerY, double scale,
                           float screenW, float screenH,
                           float& outX, float& outY);

} // namespace MapMath
