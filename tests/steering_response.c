/* Compile with one game's src on the include path and its assist.c.
   This exercises the same entry point the steering adapter calls. */
#include "assist.h"
#include "direct_rack.h"
#include <stdio.h>
#include <stdlib.h>

static void measure(const char *name, float speed, float limit, float stick, int hz) {
    AssistConfig cfg = assist_defaults();
    cfg.strength = 1.5f;
    cfg.deadzone = .04f;
    cfg.front_slip_deg = 12.f;
    cfg.steering_speed = cfg.release_speed = 1.f;
    const RackGeometry geometry = {2.6f, 1.6f};
    DirectRack rack = {0};
    int half = 0, ninety = 0;
    float first = 0;
    for (int frame = 1; frame <= hz * 4; ++frame) {
        rack_update(&rack, stick, speed, 0, 0, 0, geometry, limit, 1.f/hz, 0, cfg);
        if (frame == 1) first = rack.angle;
        if (!half && rack.angle >= rack.target * .5f) half = frame;
        if (!ninety && rack.angle >= rack.target * .9f) ninety = frame;
    }
    if (!half || !ninety || rack.target <= 0 || fabsf(rack.angle-rack.target)>.01f) exit(1);
    printf("%-16s %3d Hz  %2.0f deg limit  final %6.2f  first %5.2f  half/90%% %4.0f/%4.0f ms\n",
           name, hz, limit, rack.target, first, half*1000./hz, ninety*1000./hz);
}

int main(void) {
    const int frequencies[] = {30, 60, 120};
    for (int k = 0; k < 3; ++k) {
        int hz = frequencies[k];
        measure("park-quarter", 0, 74, .25f, hz);
        measure("park-standard", 0, 40, 1, hz);
        measure("park-FD", 0, 60, 1, hz);
        measure("park-SLR", 0, 74, 1, hz);
        measure("10 km/h", 10.f/3.6f, 74, 1, hz);
        measure("27 km/h", 7.5f, 74, 1, hz);
        measure("36 km/h", 10, 74, 1, hz);
    }
    return 0;
}
