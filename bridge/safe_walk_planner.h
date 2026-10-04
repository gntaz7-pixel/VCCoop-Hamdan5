#pragma once
// BullyCoop Hamdan v0.15c: pure, platform-independent 2D movement planner.
// Waypoints NEVER directly set ped position. No promise of engine pathfinding,
// obstacles, or navigation mesh; safe only for flat, open-area opt-in tests.
#include <cmath>

namespace BullySafeWalk {
static constexpr float kLocalFollowMaxDistance=24.0f;

struct XY { float x, y; };
enum class Status { Move, Reached, Unsafe };
struct Plan {
    Status status;
    XY waypoint;
    bool detour;
    bool catchup;
    float remaining;
    float step;
    float gapJimmy;
    float segmentJimmy;
};
inline float length(XY a) { return std::sqrt(a.x*a.x + a.y*a.y); }
inline XY sub(XY a,XY b) { return {a.x-b.x,a.y-b.y}; }
inline XY add(XY a,XY b) { return {a.x+b.x,a.y+b.y}; }
inline XY mul(XY a,float n) { return {a.x*n,a.y*n}; }
inline float clamp(float n,float lo,float hi) { return std::fmax(lo,std::fmin(n,hi)); }
inline float segmentGap(XY start,XY end,XY center) {
    XY v=sub(end,start), w=sub(center,start);
    float vv=v.x*v.x+v.y*v.y;
    float t=vv>0.0001f ? clamp((w.x*v.x+w.y*v.y)/vv,0.0f,1.0f) : 0.0f;
    return length(sub(add(start,mul(v,t)),center));
}
// v0.16b SINGLE-PC safety window. When the host is paused in the background
// its NPC cannot chase a guest running a whole district away. Pausing new tasks
// is safer than commanding NPCs to cross unloaded geometry.
inline bool localFollowInRange(XY npc, XY guest, XY jimmy) {
    const float a=length(sub(npc,jimmy));
    const float b=length(sub(guest,jimmy));
    return std::isfinite(a) && std::isfinite(b) &&
        a <= kLocalFollowMaxDistance && b <= kLocalFollowMaxDistance;
}
// A new task identical to the previous one, without observable NPC progress,
// should not be spammed at every timer tick (can overflow/reject task stack).
inline bool sameUnprogressedTask(XY npc,XY lastNpc,XY nextWaypoint,XY lastWaypoint) {
    return length(sub(npc,lastNpc)) < 0.20f &&
           length(sub(nextWaypoint,lastWaypoint)) < 0.90f;
}
inline Plan compute(XY npc, XY target, XY jimmy) {
    constexpr float kMaxTaskStep=5.8f;
    constexpr float kJimmySafeSegment=3.1f;
    constexpr float kJimmySafeEndpoint=3.4f;
    constexpr float kSafeRadius=3.9f;
    const float npcGap=length(sub(npc,jimmy));
    const float rawGap=length(sub(target,jimmy));
    const float rawDist=length(sub(target,npc));
    Plan p={Status::Unsafe,npc,false,false,rawDist,0.0f,npcGap,0.0f};
    if (!std::isfinite(npcGap) || !std::isfinite(rawGap) || !std::isfinite(rawDist) ||
        npcGap < 2.75f || rawGap < 0.05f) return p;
    // Follow from beyond Jimmy's collision zone even if extrapolated guest target
    // points directly at Jimmy. Never steer toward a target INSIDE safety zone.
    if (rawGap < kSafeRadius) target=add(jimmy,mul(sub(target,jimmy),kSafeRadius/rawGap));
    XY toGoal=sub(target,npc);
    const float remaining=length(toGoal);
    p.remaining=remaining;
    if (remaining < 0.75f) {p.status=Status::Reached;return p;}
    // Follow in bounded hops; remote target can be 20m away without invalidating
    // the whole task simply because original direct distance was >8m.
    XY direct=add(npc,mul(toGoal,std::fmin(remaining,kMaxTaskStep)/remaining));
    float candidateGap=length(sub(direct,jimmy));
    float chordGap=segmentGap(npc,direct,jimmy);
    if (candidateGap >= kJimmySafeEndpoint && chordGap >= kJimmySafeSegment) {
        p.status=Status::Move;
        p.waypoint=direct;
        p.catchup=remaining>kMaxTaskStep;
        p.step=length(sub(direct,npc));
        p.gapJimmy=candidateGap;
        p.segmentJimmy=chordGap;
        return p;
    }
    // Never drive directly THROUGH Jimmy. First steer tangentially around a
    // protective orbit while gradually moving toward target's safe radius.
    XY r0=sub(npc,jimmy), rt=sub(target,jimmy);
    float a0=std::atan2(r0.y,r0.x), at=std::atan2(rt.y,rt.x);
    float delta=std::atan2(std::sin(at-a0),std::cos(at-a0));
    const float radius=std::fmax(npcGap,kSafeRadius);
    float radiusNext=radius + clamp(length(rt)-radius,-1.0f,1.0f);
    radiusNext=std::fmax(kSafeRadius,radiusNext);
    const float maxAngle=std::fmin(0.42f,4.0f/std::fmax(radius,radiusNext));
    float angularStep=clamp(delta,-maxAngle,maxAngle);
    // When already a little too close to Jimmy, first move radially OUT.
    if (npcGap < kSafeRadius && radiusNext <= radius) radiusNext=kSafeRadius;
    XY candidate={jimmy.x + radiusNext*std::cos(a0+angularStep),
                  jimmy.y + radiusNext*std::sin(a0+angularStep)};
    const float step=length(sub(candidate,npc));
    candidateGap=length(sub(candidate,jimmy));
    chordGap=segmentGap(npc,candidate,jimmy);
    if (step < 0.65f || step > kMaxTaskStep+0.05f ||
        candidateGap<kJimmySafeEndpoint || chordGap<kJimmySafeSegment) return p;
    p.status=Status::Move;
    p.waypoint=candidate;
    p.detour=true;
    p.catchup=remaining>kMaxTaskStep;
    p.step=step;
    p.gapJimmy=candidateGap;
    p.segmentJimmy=chordGap;
    return p;
}
} // namespace BullySafeWalk
