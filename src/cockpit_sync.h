/* FH2 1.0.0.64. Capture one completed physics step, then consume it on
   the presentation thread. Both callers already hold manual_lock, which
   protects the owning WheelBinding against reassignment or destruction. */
typedef struct {
    SRWLOCK lock;
    ULONGLONG tick;
    float left, right;
    int valid;
} CockpitWheels;

static volatile LONG cockpit_enabled=1;
static void *cockpit_player_car;
static const float cockpit_half_turn=1.5707963267948966f; /* native one-sided travel */

static void cockpit_capture(CockpitWheels *sample, float left, float right){
    AcquireSRWLockExclusive(&sample->lock);
    sample->left=left;
    sample->right=right;
    sample->tick=GetTickCount64();
    sample->valid=1;
    ReleaseSRWLockExclusive(&sample->lock);
}

static int cockpit_angle(float left,float right,float limit_deg,float *out){
    if(!isfinite(left) || !isfinite(right) || !isfinite(limit_deg) ||
       limit_deg<=0 || limit_deg>90 || left*right<0)return 0;
    const float limit_rad=limit_deg*0.017453292519943295f;
    const float inside=fabsf(left)>fabsf(right)?left:right;
    if(fabsf(inside)>limit_rad+0.0001f)return 0;
    *out=fmaxf(-1,fminf(1,inside/limit_rad))*cockpit_half_turn;
    return 1;
}
