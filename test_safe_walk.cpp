#include "safe_walk_planner.h"
#include <cstdio>
#include <cmath>
using namespace BullySafeWalk;
static bool verify(const char* name, XY npc,XY dest,XY jimmy,Status expected) {
    Plan p=compute(npc,dest,jimmy);
    bool pass=p.status==expected;
    if (p.status==Status::Move) {
        pass=pass && length(sub(p.waypoint,npc))<=5.86f && p.segmentJimmy >=3.09f && p.gapJimmy>=3.39f;
    }
    std::printf("%s: %s mode=%d from=%.2f,%.2f to=%.2f,%.2f chord=%.2f step=%.2f\\n",pass?"PASS":"FAIL",name,(int)p.status,npc.x,npc.y,p.waypoint.x,p.waypoint.y,p.segmentJimmy,p.step);
    return pass;
}
int main() {
    int failed=0;
    if (!localFollowInRange({5,1},{22,0},{0,0})) { std::printf("FAIL: local near positions rejected\n"); ++failed; }
    if (localFollowInRange({5,1},{70,0},{0,0})) { std::printf("FAIL: far guest not blocked\n"); ++failed; }
    if (localFollowInRange({27,0},{9,0},{0,0})) { std::printf("FAIL: far NPC not blocked\n"); ++failed; }
    if (!sameUnprogressedTask({5.1f,0},{5,0},{11.2f,0},{11,0})) { std::printf("FAIL: unchanged task not held\n"); ++failed; }
    if (sameUnprogressedTask({5.8f,0},{5,0},{11,0},{11,0})) { std::printf("FAIL: progressed NPC incorrectly held\n"); ++failed; }
    if (sameUnprogressedTask({5,0},{5,0},{16,0},{11,0})) { std::printf("FAIL: new waypoint incorrectly held\n"); ++failed; }
    // No limit-8 deadlock when guest is far from the NPC and path is clear.
    if (!verify("far direct hop",{0,10},{30,10},{0,0},Status::Move)) ++failed;
    // Opposite sides of Jimmy. Never walk THROUGH Jimmy, even if guest is far.
    if (!verify("long Jimmy detour",{-18,0},{5,0},{0,0},Status::Move)) ++failed;
    if (!verify("short Jimmy detour",{-5,0},{5,0},{0,0},Status::Move)) ++failed;
    // Both endpoints outside Jimmy's safety zone but chord crosses it.
    if (!verify("diagonal crossing",{-12,-1},{8,4},{0,0},Status::Move)) ++failed;
    if (!verify("close to guest",{6,0},{6.1,0},{0,0},Status::Reached)) ++failed;
    // NPC already too close to Jimmy: do not risk any new task.
    if (!verify("Jimmy protected",{1,0},{8,0},{0,0},Status::Unsafe)) ++failed;
    if (!verify("predict target inside Jimmy",{-9,0},{1,0},{0,0},Status::Move)) ++failed;
    // Simulate a guest orbit (with Jimmy stationary), checking every issued step.
    for (int c=0;c<240;c++) {
        const float t=c*0.075f;
        XY guest={4*std::cos(t),4*std::sin(t)};
        XY jimmy={0,0};
        XY npc={-17,0};
        for(int i=0;i<40;i++) {
            Plan p=compute(npc,guest,jimmy);
            if(p.status==Status::Move) {
                if (p.segmentJimmy<3.09f || p.gapJimmy<3.39f || p.step>5.86f) {++failed;break;}
                npc=p.waypoint;
            } else if(p.status==Status::Reached) break;
            else {++failed;break;}
        }
    }
    std::printf("PLANNER RESULTS: %s; failures=%d\\n", failed?"FAIL":"PASS",failed);
    return failed?1:0;
}
