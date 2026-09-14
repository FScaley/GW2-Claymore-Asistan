#pragma once
#include "MapMath.h"
#include "../core/FunctionHandler.h"
#include "../mumble/Mumble.h"
#include "../nexus/Nexus.h"
#include <vector>
#include <map>
#include <string>

class MarkerOverlay {
public:
    void SetTarget(const std::vector<EntityCoord>& coords,
                   const std::map<int, MapRects>& rects);
    void ClearTarget();
    bool HasTarget() const { return m_active; }

    void Render(Mumble::Data* mumble, Mumble::Identity* ident,
                NexusLinkData_t* nexus);

private:
    void RenderMode3D(Mumble::Data* mumble, Mumble::Identity* ident,
                      NexusLinkData_t* nexus, const EntityCoord& target,
                      const MapRects& rects);
    void RenderMode3DDim(Mumble::Data* mumble, Mumble::Identity* ident,
                         NexusLinkData_t* nexus, const EntityCoord& target,
                         const MapRects& rects);
    void RenderModeMap(Mumble::Data* mumble, NexusLinkData_t* nexus,
                       const EntityCoord& target);
    void RenderModeMapDim(Mumble::Data* mumble, NexusLinkData_t* nexus,
                          const EntityCoord& target);
    void RenderHUD(Mumble::Data* mumble, NexusLinkData_t* nexus,
                   const EntityCoord& target, const MapRects* rects,
                   bool sameMap, const std::vector<const EntityCoord*>& allEntries);
    void RenderEdgeArrow(float screenW, float screenH,
                         float bearing, float dist, const std::string& label);

    std::vector<EntityCoord> m_coords;
    std::map<int, MapRects> m_rects;
    bool m_active = false;
};
