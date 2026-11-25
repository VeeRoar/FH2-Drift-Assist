#ifndef DIRECT_RACK_H
#define DIRECT_RACK_H
#include <math.h>
typedef struct {float wheelbase,track;} RackGeometry;
typedef struct {
    float angle,target;
    int valid;
    float engagement,neutral,hand;
    float left_range,right_range;
    float front_flow,window_mix;
    float yaw_magnitude;
} DirectRack;
static float rack_clip(float x,float lo,float hi){return fminf(hi,fmaxf(lo,x));}
static float rack_blend(float v,float lo,float hi){
    float t=rack_clip((v-lo)/(hi-lo),0,1); return t*t*(3-2*t);
}
/* The FH2 solver takes an inside-wheel command. Convert an outside-wheel
   boundary through the inverse of its quarter-Ackermann calculation. */
static float wheel_bound(float wheel_deg,int right_wheel,float curvature,float limit){
    float a=rack_clip(wheel_deg,-limit,limit);
    if((a>=0)==right_wheel)return a;
    float mag=fabsf(a)*.01745329252f;
    float rack=atan2f(sinf(mag),cosf(mag)-curvature*sinf(mag))*57.29578f;
    return copysignf(fminf(rack,limit),a);
}
static void rack_drive(DirectRack *r,float stick,float limit,float dt,AssistConfig cfg,float ground_speed){
    float driver=copysignf(rack_clip((fabsf(stick)-cfg.deadzone)/(1.f-cfg.deadzone),0,1),stick);
    r->hand=driver*(driver<0?r->left_range:r->right_range);
    /* A stick returning through centre blends into the released response. */
    float speed=cfg.release_speed+(cfg.steering_speed-cfg.release_speed)*rack_blend(fabsf(driver),0,.15f);
    /* CarX Combined has SteerSmoothK1=4, SteerSmoothK2=1. The native
       rack limits the angular step against the previous angle. Use the
       Forza body yaw for its rotation term; fade it in above 1 m/s. */
    float slow_rate=(4.f+r->yaw_magnitude*rack_clip(ground_speed-1.f,0,1))*57.29578f;
    float drift_rate=560.f+260.f*rack_blend(fabsf(driver),.15f,.85f);
    float moving=rack_blend(ground_speed,6.f,9.f);
    float slide=rack_blend(ground_speed,3.f,9.f)*r->engagement;
    float quick=moving+(1.f-moving)*slide;
    float rate=(slow_rate+(drift_rate-slow_rate)*quick)*speed;
    /* Local pad calibration: 150 ms to half a small requested rack change
       at rest. The target remains proportional to stick travel. As speed
       rises, restore the response used by the original drift controller. */
    const float parking_tau=.150f/.69314718f;
    float tau=.024f+(parking_tau-.024f)*(1.f-quick);
    r->target=rack_clip(r->neutral+r->hand,-limit,limit);
    float delta=(r->target-r->angle)*(-expm1f(-dt*speed/tau));
    r->angle+=rack_clip(delta,-rate*dt,rate*dt);
}
/* When support is lost there is no reliable contact-patch direction.
   Carry forward the last supported steering interval, including both
   directions of manual travel. Input and the physical rack remain live. */
static void rack_unsupported(DirectRack *r,float stick,float limit,float dt,float measured,AssistConfig cfg,float ground_speed){
    if(!r->valid){
        r->valid=1;
        r->angle=rack_clip(measured,-limit,limit);
        r->neutral=0;
        r->left_range=r->right_range=limit;
    }
    rack_drive(r,stick,limit,dt,cfg,ground_speed);
}
/* Angles are in degrees, yaw and sideslip rate in radians/s. The front
   axle distance is estimated from the wheelbase observed by the solver. */
static void rack_update(DirectRack *r,float stick,float forward,float lateral,
                        float yaw,float slip_rate,RackGeometry geometry,
                        float limit,float dt,float measured,AssistConfig cfg){
    if(!r->valid){r->angle=rack_clip(measured,-limit,limit);r->valid=1;}
    const float degrees=57.29578f;
    float speed=hypotf(lateral,forward);
    r->yaw_magnitude=fabsf(yaw);
    float slip=atan2f(lateral,forward)*degrees;
    /* Keep the assisted hand range through a quick transition. Fade it out
       on a genuine exit. Reverse and parking retain full steering travel. */
    float moving=rack_blend(speed,3,9);
    float facing=rack_blend(forward/fmaxf(speed,1),-.18f,.15f);
    float drift=moving*facing*rack_blend(fabsf(slip),5,20);
    float response=drift>r->engagement?.045f:.22f;
    r->engagement+=(drift-r->engagement)*(-expm1f(-dt/response));
    float strength=rack_clip(cfg.strength,0,1.5f);
    float maximum=rack_clip(cfg.max_correction,0,1);
    float assist=r->engagement*moving*facing*fminf(strength,1);
    float blend=assist*maximum;

    /* Steer towards the travel direction at the front axle. As sideslip
       collapses, lead the unwind; a neutral driver never creates opposite
       lock on the new side until sideslip has actually changed sign. */
    /* NativeCarX averages directions at the two steering wheels. Moving
       point velocities use v + omega x offset (also used by AC AGA). */
    float cross=lateral+.5f*geometry.wheelbase*yaw;
    float fore_delta=.5f*geometry.track*yaw;
    float left=atan2f(cross,fmaxf(.1f,forward+fore_delta))*degrees;
    float right=atan2f(cross,fmaxf(.1f,forward-fore_delta))*degrees;
    float flow=.5f*(left+right);
    r->front_flow=flow;
    float lead=rack_clip(slip_rate*degrees*.09f*rack_clip(cfg.damping,0,1.5f),-10,10);
    float alignment=(flow+lead)*(.82f+.12f*strength);
    float same_side=copysignf(fmaxf(0,alignment*copysignf(1,slip)),slip);
    float automatic=rack_clip(same_side,-limit*maximum,limit*maximum);
    r->neutral=assist*rack_blend(fabsf(slip),0,4)*automatic;

    /* CarX2 parameter 116 is the front tyre slip angle in degrees. The
       common native helper blends the endpoints quadratically over three
       speed units above its initialized threshold of six. */
    const float tyre_slip=cfg.front_slip_deg;
    float curvature=geometry.wheelbase>0?.25f*geometry.track/geometry.wheelbase:0;
    float lo=fmaxf(wheel_bound(left-tyre_slip,0,curvature,limit),
                   wheel_bound(right-tyre_slip,1,curvature,limit));
    float hi=fminf(wheel_bound(left+tyre_slip,0,curvature,limit),
                   wheel_bound(right+tyre_slip,1,curvature,limit));
    if(lo>hi){float compromise=.5f*(lo+hi);lo=hi=compromise;}
    float t=rack_clip((hypotf(cross,forward)-6.f)/3.f,0,1);
    float mix=t*t*facing*fminf(strength,1)*maximum;
    r->window_mix=mix;
    lo=-limit+mix*(lo+limit);
    hi= limit+mix*(hi-limit);
    r->neutral=rack_clip(r->neutral,lo,hi);
    float authority=1.f-blend*(1.f-rack_clip(cfg.manual,.2f,1));
    r->left_range=(r->neutral-lo)*authority;
    r->right_range=(hi-r->neutral)*authority;
    if(slip!=0){
        /* The driver is already countersteering with the front wheels.
           More lock helps catch a growing slide, but can flick the rear
           across when the same key stays down during recovery. */
        float growth=copysignf(1,slip)*slip_rate*degrees;
        float catch_fraction=.55f+.20f*rack_blend(growth,5,40)-.22f*rack_blend(-growth,5,40);
        float previous_trim=7.f+15.f*rack_clip(cfg.manual,.2f,1);
        float catch_range=limit+blend*(fminf(previous_trim*catch_fraction,limit)-limit);
        if(slip>0)r->right_range=fminf(r->right_range,catch_range);
        else r->left_range=fminf(r->left_range,catch_range);
    }
    rack_drive(r,stick,limit,dt,cfg,speed);
}
#endif
