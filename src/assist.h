#ifndef FH2_DRIFT_ASSIST_H
#define FH2_DRIFT_ASSIST_H
/* Portable control law. Inputs: local right/forward velocity in m/s,
   positive-right heading in radians, raw stick positive right, seconds. */
typedef struct {
    float strength, damping, max_correction, manual;
    int enabled;
    float steering_speed, release_speed, front_slip_deg, deadzone;
} AssistConfig;
typedef struct { float hold_angle; float heading, yaw, correction, engagement, slip, slip_rate, transition, recovery; float previous_stick, previous_added, crossing_timer; int initialized; } AssistState;
typedef struct { float stick, lateral, forward, heading, dt; int grounded, active; } AssistInput;
typedef struct { float steering, slip, yaw, engagement; } AssistOutput;
AssistConfig assist_defaults(void);
AssistOutput assist_step(AssistState*, const AssistConfig*, AssistInput);
#endif
