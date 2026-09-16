#include "MarkerOverlay.h"
#include "../imgui/imgui.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

static const ImVec4 COL_GOLD      = ImVec4(0.93f, 0.91f, 0.67f, 1.0f);
static const ImVec4 COL_GOLD_DIM  = ImVec4(0.93f, 0.91f, 0.67f, 0.55f);
static const ImVec4 COL_WHITE     = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
static const ImVec4 COL_WHITE_DIM = ImVec4(1.0f, 1.0f, 1.0f, 0.4f);
static const ImU32  U32_GOLD      = IM_COL32(238, 232, 170, 255);
static const ImU32  U32_GOLD_GLOW = IM_COL32(238, 232, 170, 45);
static const ImU32  U32_OUTLINE   = IM_COL32(0, 0, 0, 180);
static const ImU32  U32_HUD_BG    = IM_COL32(10, 9, 8, 210);
static const ImU32  U32_HUD_BORDER= IM_COL32(238, 232, 170, 65);

static constexpr float PI = 3.14159265358979323846f;

void MarkerOverlay::SetTarget(const std::vector<EntityCoord>& coords,
                               const std::map<int, MapRects>& rects)
{
    m_coords = coords;
    m_rects = rects;
    m_active = !m_coords.empty();
}

void MarkerOverlay::ClearTarget()
{
    m_coords.clear();
    m_rects.clear();
    m_active = false;
}

void MarkerOverlay::Render(Mumble::Data* mumble, Mumble::Identity* ident,
                            NexusLinkData_t* nexus)
{
    if (!m_active || !mumble || !nexus || !nexus->IsGameplay) return;

    unsigned mapId = mumble->Context.MapID;

    // Collect all entries for the current map
    std::vector<const EntityCoord*> sameMapEntries;
    for (auto& ec : m_coords)
        if (ec.mapId == static_cast<int>(mapId)) sameMapEntries.push_back(&ec);

    bool sameMap = !sameMapEntries.empty();

    if (mumble->Context.IsMapOpen) {
        // World map open: show ALL locations across all maps
        for (auto& ec : m_coords) {
            if (!ec.hasCoord || ec.done) continue;
            if (ec.source == EntityCoord::Exact)
                RenderModeMap(mumble, nexus, ec);
            else if (ec.source == EntityCoord::Sector)
                RenderModeMapDim(mumble, nexus, ec);
        }
    } else if (sameMap) {
        // Gameplay: show only current map markers as 3D
        for (auto* ec : sameMapEntries) {
            if (!ec->hasCoord || ec->done) continue;
            auto it = m_rects.find(ec->mapId);
            if (it == m_rects.end()) continue;
            const MapRects& rects = it->second;

            if (ec->source == EntityCoord::Exact)
                RenderMode3D(mumble, ident, nexus, *ec, rects);
            else if (ec->source == EntityCoord::Sector)
                RenderMode3DDim(mumble, ident, nexus, *ec, rects);
        }
    }

    // HUD: pick the best entry for text
    const EntityCoord* hudTarget = nullptr;
    if (sameMap) {
        for (auto* ec : sameMapEntries)
            if (ec->source == EntityCoord::Exact) { hudTarget = ec; break; }
        if (!hudTarget && !sameMapEntries.empty()) hudTarget = sameMapEntries[0];
    }
    if (!hudTarget) {
        for (auto& ec : m_coords)
            if (ec.hasCoord) { hudTarget = &ec; break; }
    }
    if (!hudTarget && !m_coords.empty()) hudTarget = &m_coords[0];
    if (!hudTarget) return;

    const MapRects* hudRects = nullptr;
    auto it = m_rects.find(hudTarget->mapId);
    if (it != m_rects.end()) hudRects = &it->second;

    RenderHUD(mumble, nexus, *hudTarget, hudRects, sameMap, sameMapEntries);
}

void MarkerOverlay::RenderMode3D(Mumble::Data* mumble, Mumble::Identity* ident,
                                  NexusLinkData_t* nexus, const EntityCoord& target,
                                  const MapRects& rects)
{
    float screenW = static_cast<float>(nexus->Width);
    float screenH = static_cast<float>(nexus->Height);

    float worldX, worldZ;
    MapMath::ContinentToWorldXZ(target.cx, target.cy, rects.contRect, rects.mapRect, worldX, worldZ);
    float worldY = mumble->AvatarPosition.Y;

    float fov = (ident && ident->FOV > 0.01f) ? ident->FOV : MapMath::DEFAULT_FOV;

    MapMath::Vec3 camPos = {mumble->CameraPosition.X, mumble->CameraPosition.Y, mumble->CameraPosition.Z};
    MapMath::Vec3 camFront = {mumble->CameraFront.X, mumble->CameraFront.Y, mumble->CameraFront.Z};
    MapMath::Vec3 camTop = {mumble->CameraTop.X, mumble->CameraTop.Y, mumble->CameraTop.Z};

    MapMath::Mat4 vp = MapMath::BuildViewProj(camPos, camFront, camTop, fov, screenW, screenH);

    float sx, sy, depth;
    MapMath::Vec3 entityPos = {worldX, worldY, worldZ};
    bool visible = MapMath::WorldToScreen(entityPos, vp, screenW, screenH, sx, sy, depth);

    float dx = worldX - camPos.x;
    float dy = worldY - camPos.y;
    float dz = worldZ - camPos.z;
    float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (visible) {
        float markerSize = MapMath::PerspectiveSize(2.5f, dist, fov, screenH);

        dl->AddCircleFilled(ImVec2(sx, sy), markerSize * 0.7f, U32_GOLD_GLOW, 32);
        dl->AddCircle(ImVec2(sx, sy), markerSize * 0.5f, U32_OUTLINE, 32, 3.0f);
        dl->AddCircle(ImVec2(sx, sy), markerSize * 0.5f, U32_GOLD, 32, 2.0f);
        dl->AddCircleFilled(ImVec2(sx, sy), std::max(3.0f, markerSize * 0.12f), U32_GOLD, 16);

        // Label: show always for few markers, mouse-proximity for many
        bool showLabel = (m_coords.size() <= 10);
        if (!showLabel) {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            float mdx = mousePos.x - sx, mdy = mousePos.y - sy;
            showLabel = (mdx * mdx + mdy * mdy < 120.f * 120.f);
        }
        if (showLabel) {
            char label[128];
            snprintf(label, sizeof(label), "%s", target.name.c_str());
            ImVec2 textSize = ImGui::CalcTextSize(label);
            float lx = sx - textSize.x * 0.5f;
            float ly = sy + markerSize * 0.5f + 6.0f;
            dl->AddRectFilled(ImVec2(lx - 4, ly - 2), ImVec2(lx + textSize.x + 4, ly + textSize.y + 2),
                              U32_HUD_BG, 2.0f);
            dl->AddRect(ImVec2(lx - 4, ly - 2), ImVec2(lx + textSize.x + 4, ly + textSize.y + 2),
                        U32_HUD_BORDER, 2.0f);
            dl->AddText(ImVec2(lx, ly), U32_GOLD, label);
        }
    } else {
        // Off-screen: compute bearing for edge arrow
        // Player continent position from Compass
        double playerCX = mumble->Context.Compass.PlayerPosition.X;
        double playerCY = mumble->Context.Compass.PlayerPosition.Y;
        double bearing = MapMath::Bearing(playerCX, playerCY, target.cx, target.cy);

        // Camera facing in continent space (approximate from CameraFront XZ)
        double camBearing = std::atan2(camFront.x, camFront.z);
        if (camBearing < 0) camBearing += 2.0 * PI;

        RenderEdgeArrow(screenW, screenH, static_cast<float>(bearing - camBearing), dist, target.name);
    }
}

void MarkerOverlay::RenderModeMap(Mumble::Data* mumble, NexusLinkData_t* nexus,
                                   const EntityCoord& target)
{
    float screenW = static_cast<float>(nexus->Width);
    float screenH = static_cast<float>(nexus->Height);
    double scale = mumble->Context.Compass.Scale;
    double centerX = mumble->Context.Compass.Center.X;
    double centerY = mumble->Context.Compass.Center.Y;

    float mx, my;
    if (!MapMath::ContinentToMapScreen(target.cx, target.cy, centerX, centerY, scale,
                                        screenW, screenH, mx, my))
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float r = 12.0f;

    dl->AddCircleFilled(ImVec2(mx, my), r + 2.0f, U32_OUTLINE, 24);
    dl->AddCircleFilled(ImVec2(mx, my), r, U32_GOLD_GLOW, 24);
    dl->AddCircle(ImVec2(mx, my), r, U32_GOLD, 24, 2.0f);
    dl->AddCircleFilled(ImVec2(mx, my), 3.0f, U32_GOLD, 12);

    char label[128];
    snprintf(label, sizeof(label), "%s", target.name.c_str());
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float lx = mx - textSize.x * 0.5f;
    float ly = my + r + 4.0f;
    dl->AddRectFilled(ImVec2(lx - 4, ly - 1), ImVec2(lx + textSize.x + 4, ly + textSize.y + 1),
                      U32_HUD_BG, 2.0f);
    dl->AddText(ImVec2(lx, ly), U32_GOLD, label);
}

void MarkerOverlay::RenderMode3DDim(Mumble::Data* mumble, Mumble::Identity* ident,
                                     NexusLinkData_t* nexus, const EntityCoord& target,
                                     const MapRects& rects)
{
    float screenW = static_cast<float>(nexus->Width);
    float screenH = static_cast<float>(nexus->Height);

    float worldX, worldZ;
    MapMath::ContinentToWorldXZ(target.cx, target.cy, rects.contRect, rects.mapRect, worldX, worldZ);
    float worldY = mumble->AvatarPosition.Y;

    float fov = (ident && ident->FOV > 0.01f) ? ident->FOV : MapMath::DEFAULT_FOV;
    MapMath::Vec3 camPos = {mumble->CameraPosition.X, mumble->CameraPosition.Y, mumble->CameraPosition.Z};
    MapMath::Vec3 camFront = {mumble->CameraFront.X, mumble->CameraFront.Y, mumble->CameraFront.Z};
    MapMath::Vec3 camTop = {mumble->CameraTop.X, mumble->CameraTop.Y, mumble->CameraTop.Z};
    MapMath::Mat4 vp = MapMath::BuildViewProj(camPos, camFront, camTop, fov, screenW, screenH);

    float dx = worldX - camPos.x, dz = worldZ - camPos.z;
    float dist = std::sqrt(dx*dx + dz*dz);

    float sx, sy, depth;
    bool visible = MapMath::WorldToScreen({worldX, worldY, worldZ}, vp, screenW, screenH, sx, sy, depth);
    std::string label = target.areas.empty() ? target.name : target.areas[0];

    if (visible) {
        float markerSize = MapMath::PerspectiveSize(2.5f, dist, fov, screenH);
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImU32 dimRing = IM_COL32(238, 232, 170, 120);
        ImU32 dimGlow = IM_COL32(238, 232, 170, 20);

        dl->AddCircleFilled(ImVec2(sx, sy), markerSize * 0.6f, dimGlow, 24);
        dl->AddCircle(ImVec2(sx, sy), markerSize * 0.45f, U32_OUTLINE, 24, 2.5f);
        dl->AddCircle(ImVec2(sx, sy), markerSize * 0.45f, dimRing, 24, 1.5f);

        ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        float lx = sx - textSize.x * 0.5f;
        float ly = sy + markerSize * 0.45f + 5.0f;
        dl->AddRectFilled(ImVec2(lx - 3, ly - 1), ImVec2(lx + textSize.x + 3, ly + textSize.y + 1),
                          U32_HUD_BG, 2.0f);
        dl->AddText(ImVec2(lx, ly), dimRing, label.c_str());
    } else {
        double playerCX = mumble->Context.Compass.PlayerPosition.X;
        double playerCY = mumble->Context.Compass.PlayerPosition.Y;
        double bearing = MapMath::Bearing(playerCX, playerCY, target.cx, target.cy);
        double camBearing = std::atan2(static_cast<double>(camFront.x), static_cast<double>(camFront.z));
        if (camBearing < 0) camBearing += 2.0 * PI;
        RenderEdgeArrow(screenW, screenH, static_cast<float>(bearing - camBearing), dist, label);
    }
}

void MarkerOverlay::RenderModeMapDim(Mumble::Data* mumble, NexusLinkData_t* nexus,
                                      const EntityCoord& target)
{
    float screenW = static_cast<float>(nexus->Width);
    float screenH = static_cast<float>(nexus->Height);
    double scale = mumble->Context.Compass.Scale;
    double centerX = mumble->Context.Compass.Center.X;
    double centerY = mumble->Context.Compass.Center.Y;

    float mx, my;
    if (!MapMath::ContinentToMapScreen(target.cx, target.cy, centerX, centerY, scale,
                                        screenW, screenH, mx, my))
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImU32 dimRing = IM_COL32(238, 232, 170, 120);
    float r = 10.0f;

    dl->AddCircleFilled(ImVec2(mx, my), r + 1.5f, U32_OUTLINE, 20);
    dl->AddCircle(ImVec2(mx, my), r, dimRing, 20, 1.5f);

    std::string label = target.areas.empty() ? target.name : target.areas[0];
    ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    float lx = mx - textSize.x * 0.5f;
    float ly = my + r + 3.0f;
    dl->AddRectFilled(ImVec2(lx - 3, ly - 1), ImVec2(lx + textSize.x + 3, ly + textSize.y + 1),
                      U32_HUD_BG, 2.0f);
    dl->AddText(ImVec2(lx, ly), dimRing, label.c_str());
}

void MarkerOverlay::RenderHUD(Mumble::Data* mumble, NexusLinkData_t* nexus,
                               const EntityCoord& target, const MapRects* rects,
                               bool sameMap,
                               const std::vector<const EntityCoord*>& allEntries)
{
    float screenW = static_cast<float>(nexus->Width);

    // Distance uses PlayerPosition (not Center, which follows pan)
    double playerCX = mumble->Context.Compass.PlayerPosition.X;
    double playerCY = mumble->Context.Compass.PlayerPosition.Y;

    bool isCollection = m_coords.size() > 6;
    int totalCoords = 0, sameMapCoords = 0, doneCount = 0;
    for (auto& ec : m_coords) {
        if (ec.hasCoord) { ++totalCoords; if (ec.mapId == static_cast<int>(mumble->Context.MapID)) ++sameMapCoords; }
        if (ec.done) ++doneCount;
    }
    int remaining = totalCoords - doneCount;

    char hudText[256];
    if (isCollection && sameMap) {
        if (doneCount > 0)
            snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 bu haritada %d konum (%d/%d kalan)",
                     target.name.c_str(), sameMapCoords, remaining, totalCoords);
        else
            snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 bu haritada %d konum (%d toplam)",
                     target.name.c_str(), sameMapCoords, totalCoords);
    } else if (isCollection && !sameMap) {
        if (doneCount > 0)
            snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 %d/%d kalan",
                     target.name.c_str(), remaining, totalCoords);
        else
            snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 %d konum (%zu harita)",
                     target.name.c_str(), totalCoords, m_rects.size());
    } else if (sameMap && target.hasCoord && rects) {
        double dist = MapMath::ContinentDistanceMetres(
            playerCX, playerCY, target.cx, target.cy, rects->contRect, rects->mapRect);
        double bearing = MapMath::Bearing(playerCX, playerCY, target.cx, target.cy);

        double camBearing = std::atan2(
            static_cast<double>(mumble->CameraFront.X),
            static_cast<double>(mumble->CameraFront.Z));
        if (camBearing < 0) camBearing += 2.0 * PI;
        double relBearing = bearing - camBearing;

        snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 %.0f m %s %s",
                 target.name.c_str(), dist,
                 MapMath::CompassDirection8(bearing),
                 MapMath::DirectionArrow(relBearing));
    } else if (sameMap) {
        // Collect all area names from all entries on this map
        std::string areaList;
        for (auto* ec : allEntries) {
            for (auto& a : ec->areas) {
                if (!areaList.empty()) areaList += " / ";
                areaList += a;
            }
        }
        if (!areaList.empty())
            snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 %s",
                     target.name.c_str(), areaList.c_str());
        else
            snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 bu haritada",
                     target.name.c_str());
    } else if (!sameMap && !target.mapName.empty()) {
        snprintf(hudText, sizeof(hudText), "%s \xe2\x80\x94 %s",
                 target.name.c_str(), target.mapName.c_str());
    } else {
        snprintf(hudText, sizeof(hudText), "%s", target.name.c_str());
    }

    ImVec2 textSize = ImGui::CalcTextSize(hudText);
    float hudW = textSize.x + 60.0f;
    float hudH = textSize.y + 14.0f;
    float hudX = (screenW - hudW) * 0.5f;
    float hudY = 18.0f;

    ImGui::SetNextWindowPos(ImVec2(hudX, hudY));
    ImGui::SetNextWindowSize(ImVec2(hudW, hudH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.035f, 0.03f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.93f, 0.91f, 0.67f, 0.25f));

    ImGui::Begin("##claymore_marker_hud", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysAutoResize);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(hudX + 14, hudY + hudH * 0.5f), 5.0f, U32_GOLD, 12);
    dl->AddText(ImVec2(hudX + 26, hudY + 7), IM_COL32(255, 255, 255, 255), hudText);

    // Dismiss button
    float btnSize = hudH - 4.0f;
    ImGui::SetCursorScreenPos(ImVec2(hudX + hudW - btnSize - 4.0f, hudY + 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.2f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.3f, 0.2f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.6f));
    if (ImGui::Button("X", ImVec2(btnSize, btnSize)))
        ClearTarget();
    ImGui::PopStyleColor(4);

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void MarkerOverlay::RenderEdgeArrow(float screenW, float screenH,
                                     float bearing, float dist,
                                     const std::string& label)
{
    float margin = 40.0f;
    float cx = screenW * 0.5f;
    float cy = screenH * 0.5f;

    float ax = cx + std::sin(bearing) * (cx - margin);
    float ay = cy - std::cos(bearing) * (cy - margin);
    ax = std::clamp(ax, margin, screenW - margin);
    ay = std::clamp(ay, margin, screenH - margin);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Arrow triangle pointing outward
    float arrowSize = 14.0f;
    float s = std::sin(bearing), c = std::cos(bearing);
    ImVec2 tip(ax + s * arrowSize, ay - c * arrowSize);
    ImVec2 left(ax + std::sin(bearing - 2.5f) * arrowSize * 0.6f,
                ay - std::cos(bearing - 2.5f) * arrowSize * 0.6f);
    ImVec2 right(ax + std::sin(bearing + 2.5f) * arrowSize * 0.6f,
                 ay - std::cos(bearing + 2.5f) * arrowSize * 0.6f);
    dl->AddTriangleFilled(tip, left, right, U32_GOLD);

    char distText[64];
    snprintf(distText, sizeof(distText), "%.0f m", dist);
    ImVec2 ts = ImGui::CalcTextSize(distText);
    float tx = ax - ts.x * 0.5f;
    float ty = ay + arrowSize + 4.0f;
    dl->AddRectFilled(ImVec2(tx - 3, ty - 1), ImVec2(tx + ts.x + 3, ty + ts.y + 1),
                      U32_HUD_BG, 2.0f);
    dl->AddText(ImVec2(tx, ty), U32_GOLD, distText);
}
