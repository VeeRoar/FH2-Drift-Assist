#ifndef MANUAL_UNWIND_H
#define MANUAL_UNWIND_H
#include <math.h>

/* side is the sign of the old lock. lead is in degrees on that axis. */
typedef struct { float side, start, lead, previous; int releasing; } ManualUnwind;

static float manual_angle(float demand,ManualUnwind s){
    if(!s.side || s.lead<=0)return demand;
    float native=s.side*demand;
    float shifted=native-s.lead;
    /* Unit slope at zero; progressively soften the first 7 degrees of
       opposite lock. Hand back when native steering gets there itself. */
    if(shifted<0)shifted=7.f*expm1f(shifted/7.f);
    return s.side*fminf(native,shifted);
}

static void manual_update(ManualUnwind *s,float stick,float slip,float demand,float dt){
    if(!isfinite(stick)||!isfinite(slip)||!isfinite(demand)||!(dt>0 && dt<=.1f)){
        *s=(ManualUnwind){0}; return;
    }
    if(!s->side && fabsf(slip)>=18 && fabsf(demand)>=12 && stick*demand<0 && fabsf(stick)>=.80f){
        s->side=copysignf(1.f,demand); s->start=fabsf(demand); s->previous=s->start;
    }
    if(!s->side)return;
    float native=s->side*demand;
    if(native<=-7.f){*s=(ManualUnwind){0};return;}
    if(stick*s->side>-.60f)s->releasing=1;
    if(s->releasing){
        /* Limit the return of old-side lock, even when the native target
           moves away as the driver releases. Invert the soft crossing curve. */
        float next=s->previous+70.f*dt;
        if(next>=native){*s=(ManualUnwind){0};return;}
        float shifted=next<0?7.f*log1pf(fmaxf(-.999999f,next/7.f)):next;
        s->lead=fmaxf(0,native-shifted);
    }else{
        float progress=fmaxf(0,s->start-native);
        float goal=fminf(32.f,1.4f*progress);
        s->lead+=fmaxf(-300.f*dt,fminf(300.f*dt,goal-s->lead));
    }
    s->previous=s->side*manual_angle(demand,*s);
}
#endif
