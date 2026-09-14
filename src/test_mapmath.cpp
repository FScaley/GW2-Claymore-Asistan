// test_mapmath.exe -- offline math tests for MapMath (zero cost, no network)
// Build (VS Developer Command Prompt, from src/):
//   cl /EHsc /std:c++17 /permissive- /DNOMINMAX /MT /utf-8 test_mapmath.cpp map\MapMath.cpp
#include "map/MapMath.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

static int g_pass = 0, g_fail = 0;
static constexpr double PI = 3.14159265358979323846;

static void Check(bool cond, const char* desc) {
    if (cond) { ++g_pass; printf("  PASS: %s\n", desc); }
    else      { ++g_fail; printf("  FAIL: %s\n", desc); }
}

static void CheckNear(double a, double b, double tol, const char* desc) {
    bool ok = std::abs(a - b) <= tol;
    if (ok) { ++g_pass; printf("  PASS: %s (%.4f ~ %.4f)\n", desc, a, b); }
    else    { ++g_fail; printf("  FAIL: %s (%.4f != %.4f, tol %.4f)\n", desc, a, b, tol); }
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== test_mapmath ===\n\n");

    // --- TEST 1: ContinentToWorldXZ + WorldXZToContinent round-trip ---
    printf("TEST 1: Coordinate conversion round-trip\n");
    {
        // Tyrian Codex validation data: continent [43728, 28590] -> world (-419.5, _, 440.2)
        // Using Queensdale-like rects for a general test:
        // continent_rect: [[40960, 26624], [43008, 30720]]  (left,top,right,bottom)
        // map_rect: [[-43008, -27648], [46080, 30720]]
        double contRect[4] = {40960, 26624, 43008, 30720};
        double mapRect[4]  = {-43008, -27648, 46080, 30720};

        float wx, wz;
        MapMath::ContinentToWorldXZ(41984, 28672, contRect, mapRect, wx, wz);
        printf("    continent (41984, 28672) -> world (%.2f, _, %.2f)\n", wx, wz);

        double cx, cy;
        MapMath::WorldXZToContinent(wx, wz, contRect, mapRect, cx, cy);
        CheckNear(cx, 41984.0, 0.1, "round-trip X");
        CheckNear(cy, 28672.0, 0.1, "round-trip Y");
    }

    // --- TEST 2: Known Tyrian Codex values ---
    printf("\nTEST 2: Tyrian Codex validation (continent [43728, 28590])\n");
    {
        // The research report says: continent [43728, 28590] -> world (-419.5, _, 440.2)
        // We need the actual rects for that map to verify. Use a synthetic test instead:
        // If contRect = [43000, 28000, 44000, 29000] and mapRect = [-20000, -20000, 20000, 20000]
        // fracX = (43728-43000)/(44000-43000) = 0.728
        // fracY = (28590-28000)/(29000-28000) = 0.59
        // mx = -20000 + 0.728 * 40000 = 9120 inches
        // my = 20000 - 0.59 * 40000 = -3600 inches
        // worldX = 9120 / 39.3701 = 231.65 m
        // worldZ = -3600 / 39.3701 = -91.44 m
        double contRect[4] = {43000, 28000, 44000, 29000};
        double mapRect[4]  = {-20000, -20000, 20000, 20000};
        float wx, wz;
        MapMath::ContinentToWorldXZ(43728, 28590, contRect, mapRect, wx, wz);
        CheckNear(wx, 231.65, 0.1, "worldX");
        CheckNear(wz, -91.44, 0.1, "worldZ");

        double cx, cy;
        MapMath::WorldXZToContinent(wx, wz, contRect, mapRect, cx, cy);
        CheckNear(cx, 43728.0, 0.1, "inverse X");
        CheckNear(cy, 28590.0, 0.1, "inverse Y");
    }

    // --- TEST 3: Distance ---
    printf("\nTEST 3: ContinentDistanceMetres\n");
    {
        double contRect[4] = {0, 0, 1000, 1000};
        double mapRect[4]  = {0, 0, 39370.1, 39370.1};   // 1000m x 1000m map

        double d = MapMath::ContinentDistanceMetres(0, 0, 1000, 0, contRect, mapRect);
        CheckNear(d, 1000.0, 0.1, "1000 continent units = 1000m with these rects");

        d = MapMath::ContinentDistanceMetres(0, 0, 500, 500, contRect, mapRect);
        CheckNear(d, std::sqrt(500.0*500.0 + 500.0*500.0), 0.1, "diagonal distance");
    }

    // --- TEST 4: Bearing ---
    printf("\nTEST 4: Bearing (0=north, clockwise)\n");
    {
        // Continent: +X=east, +Y=south. North = -Y.
        // Target directly north (same X, lower Y):
        double b = MapMath::Bearing(100, 100, 100, 50);
        CheckNear(b, 0.0, 0.01, "target north -> bearing 0");

        // Target east:
        b = MapMath::Bearing(100, 100, 200, 100);
        CheckNear(b, PI / 2, 0.01, "target east -> bearing pi/2");

        // Target south:
        b = MapMath::Bearing(100, 100, 100, 200);
        CheckNear(b, PI, 0.01, "target south -> bearing pi");

        // Target west:
        b = MapMath::Bearing(100, 100, 0, 100);
        CheckNear(b, 3.0 * PI / 2, 0.01, "target west -> bearing 3pi/2");

        // Target northeast:
        b = MapMath::Bearing(100, 100, 200, 0);
        CheckNear(b, PI / 4, 0.01, "target NE -> bearing pi/4");
    }

    // --- TEST 5: CompassDirection8 ---
    printf("\nTEST 5: CompassDirection8\n");
    {
        Check(std::string(MapMath::CompassDirection8(0.0)) == "K", "0 rad = K (north)");
        Check(std::string(MapMath::CompassDirection8(PI / 2)) == "D", "pi/2 = D (east)");
        Check(std::string(MapMath::CompassDirection8(PI)) == "G", "pi = G (south)");
        Check(std::string(MapMath::CompassDirection8(3 * PI / 2)) == "B", "3pi/2 = B (west)");
        Check(std::string(MapMath::CompassDirection8(PI / 4)) == "KD", "pi/4 = KD (NE)");
        Check(std::string(MapMath::CompassDirection8(5 * PI / 4)) == "GB", "5pi/4 = GB (SW)");
    }

    // --- TEST 6: DirectionArrow ---
    printf("\nTEST 6: DirectionArrow\n");
    {
        Check(std::string(MapMath::DirectionArrow(0.0)) == "\xe2\x86\x91", "0 = up arrow");
        Check(std::string(MapMath::DirectionArrow(PI)) == "\xe2\x86\x93", "pi = down arrow");
        Check(std::string(MapMath::DirectionArrow(PI / 2)) == "\xe2\x86\x92", "pi/2 = right arrow");
    }

    // --- TEST 7: BuildViewProj + WorldToScreen ---
    printf("\nTEST 7: 3D Projection\n");
    {
        using namespace MapMath;

        Vec3 cam = {0.f, 10.f, 0.f};
        Vec3 front = {0.f, 0.f, 1.f};    // looking +Z
        Vec3 top = {0.f, 1.f, 0.f};
        float fov = 1.222f;

        Mat4 vp = BuildViewProj(cam, front, top, fov, 1920.f, 1080.f);

        // Point directly in front, 50m away
        float sx, sy, depth;
        bool vis = WorldToScreen({0.f, 10.f, 50.f}, vp, 1920.f, 1080.f, sx, sy, depth);
        Check(vis, "point 50m ahead is visible");
        CheckNear(sx, 960.0, 5.0, "centered horizontally");
        CheckNear(sy, 540.0, 5.0, "centered vertically");

        // Point behind camera
        vis = WorldToScreen({0.f, 10.f, -50.f}, vp, 1920.f, 1080.f, sx, sy, depth);
        Check(!vis, "point behind camera is NOT visible");

        // Point to the right
        vis = WorldToScreen({20.f, 10.f, 50.f}, vp, 1920.f, 1080.f, sx, sy, depth);
        Check(vis, "point to the right is visible");
        Check(sx > 960.f, "right point is right of center");

        // Point above
        vis = WorldToScreen({0.f, 30.f, 50.f}, vp, 1920.f, 1080.f, sx, sy, depth);
        Check(vis, "point above is visible");
        Check(sy < 540.f, "above point is above center");
    }

    // --- TEST 8: PerspectiveSize ---
    printf("\nTEST 8: PerspectiveSize\n");
    {
        float s1 = MapMath::PerspectiveSize(2.5f, 10.f, 1.222f, 1080.f);
        float s2 = MapMath::PerspectiveSize(2.5f, 100.f, 1.222f, 1080.f);
        Check(s1 > s2, "closer = larger");
        Check(s2 >= 18.f, "floor clamp at 18px");
        Check(s1 <= 110.f, "cap clamp at 110px");
        printf("    10m: %.1f px, 100m: %.1f px\n", s1, s2);
    }

    // --- TEST 9: ContinentToMapScreen ---
    printf("\nTEST 9: ContinentToMapScreen (2D map overlay)\n");
    {
        float mx, my;
        bool vis = MapMath::ContinentToMapScreen(
            1000, 2000,     // target
            1000, 2000,     // center = same as target
            1.0,            // scale
            1920.f, 1080.f,
            mx, my);
        Check(vis, "target at center is visible");
        CheckNear(mx, 960.0, 1.0, "centered X");
        CheckNear(my, 540.0, 1.0, "centered Y");

        // Target offset from center
        vis = MapMath::ContinentToMapScreen(
            1100, 2000,     // 100 units east of center
            1000, 2000,     // center
            2.0,            // scale
            1920.f, 1080.f,
            mx, my);
        Check(vis, "offset target visible");
        CheckNear(mx, 960.f + 50.f, 1.0, "100 units / scale 2 = 50px right of center");
        CheckNear(my, 540.0, 1.0, "same Y");
    }

    // --- Summary ---
    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
