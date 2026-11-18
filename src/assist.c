#include "assist.h"
#include <math.h>
#include <string.h>
static float bound(float x,float lo,float hi){return fminf(hi,fmaxf(lo,x));}
static float smooth(float x,float a,float b){x=bound((x-a)/(b-a),0,1);return x*x*(3-2*x);}
static float approach(float from,float to,float dt,float tau){return from+(to-from)*(-expm1f(-dt/tau));}
AssistConfig assist_defaults(void){
    return (AssistConfig){.strength=1.5f,.damping=1.5f,.max_correction=1,.manual=1,.enabled=1,
                          .steering_speed=1,.release_speed=1,.front_slip_deg=12,.deadzone=.04f};
}
AssistOutput assist_step(AssistState* s,const AssistConfig* c,AssistInput i){
    AssistOutput o={isfinite(i.stick)?bound(i.stick,-1,1):0,0,0,0};
    if(!c->enabled || c->strength<=0 || !i.active || !i.grounded || !isfinite(i.lateral) ||
       !isfinite(i.forward) || !isfinite(i.heading) || !isfinite(i.dt) ||
       !isfinite(i.stick) || i.dt<=0 || i.dt>.1f || i.forward<2 ||
       fabsf(i.lateral)>180 || i.forward>180){memset(s,0,sizeof(*s));return o;}
    if(!s->initialized){s->heading=i.heading;s->slip=atan2f(i.lateral,i.forward);s->previous_stick=i.stick;s->initialized=1;return o;}
    float delta=remainderf(i.heading-s->heading,6.28318530718f);
    s->heading=i.heading;
    /* Teleports, rewinds and discontinuities must not become a steering impulse. */
    if(fabsf(delta)>0.35f){memset(s,0,sizeof(*s));return o;}
    float yaw=bound(delta/i.dt,-5,5);
    s->yaw=approach(s->yaw,yaw,i.dt,.012f);
    float beta=atan2f(i.lateral,i.forward);
    s->slip_rate=approach(s->slip_rate,bound((beta-s->slip)/i.dt,-4,4),i.dt,.018f);
    s->slip=beta;
    float speed=hypotf(i.forward,i.lateral),fast=smooth(speed,15,40);
    float manual_takeover=smooth(fabsf(i.stick),.55f,.90f);
    float hands_off=1-smooth(fabsf(i.stick),.08f,.35f);
    /* Capture an established slide. Release on a real side change or exit,
       never retain the old side across a manual transition. */
    if(beta*s->hold_angle<=0 || fabsf(beta)<.045f)s->hold_angle=0;
    if(manual_takeover>.05f)s->hold_angle=beta;
    else if(fabsf(beta)>.16f && fabsf(s->hold_angle)<.16f)s->hold_angle=beta;
    else if(fabsf(beta)>fabsf(s->hold_angle))
        s->hold_angle=approach(s->hold_angle,beta,i.dt,.25f);
    /* About one degree per second of target unwind, not a frozen car pose. */
    float decay=.0174533f*i.dt;
    s->hold_angle=copysignf(fmaxf(0,fabsf(s->hold_angle)-decay),s->hold_angle);
    s->hold_angle=bound(s->hold_angle,-.95f,.95f);
    float gain=bound(c->strength,0,1.5f),maxc=bound(c->max_correction,0,1);
    float error=beta-s->hold_angle;
    /* Feed forward the slip angle against a 60-degree drift rack. Feedback
       unloads countersteer when the slide collapses, rather than straightening
       it faster. This is an FH2 approximation, not CarX's physics controller. */
    float target=(beta*.88f+error*.90f+s->slip_rate*.20f)/1.04719755f;
    target-=s->yaw*.018f*bound(c->damping,0,1.5f);
    target*=gain;
    /* Near exit, never force the chassis into the other drift direction. */
    float reverse_limit=.12f*smooth(fabsf(beta),.045f,.16f);
    target=beta>=0?fmaxf(-reverse_limit,target):fminf(reverse_limit,target);
    /* Only relax old lock while a large established slide is shrinking. */
    float closing = beta >= 0 ? -s->slip_rate : s->slip_rate;
    float unwind = hands_off * smooth(fabsf(s->hold_angle), .50f, .78f)
                   * smooth(closing, .18f, .85f);
    /* Fade automatic opposite lock while the car is still on the old side. */
    if (target * beta < 0) target *= 1 - unwind;
    float envelope=maxc*(1-.25f*fast);
    target=bound(target,-envelope,envelope);
    /* On a shrinking large slide, retain a modest same-side catch after a
       telemetry reset, but avoid rebuilding full lock as engagement returns. */
    float side = copysignf(1.f,beta);
    float retained = fmaxf(side*s->correction, .40f*smooth(fabsf(beta),.16f,.80f));
    float reapply = fmaxf(0.f,side*target-retained);
    target -= side*unwind*reapply;
    target*=1-manual_takeover;
    s->recovery=0;
    float desired=approach(s->correction,target,i.dt,.008f);
    float catch_rate=8-5.5f*fast*hands_off;
    /* Release old lock promptly, but build NEW hands-off lock more gently.
       Split a sign crossing at zero rather than imposing a centre dead frame. */
    float release_rate=manual_takeover>.05f?10:4+5*unwind;
    if(s->correction*desired<0){
        float to_zero=fabsf(s->correction)/release_rate;
        if(to_zero>=i.dt)s->correction-=copysignf(release_rate*i.dt,s->correction);
        else s->correction=copysignf(fminf(fabsf(desired),catch_rate*(i.dt-to_zero)),desired);
    }else{
        float rate=fabsf(desired)<fabsf(s->correction)?release_rate:catch_rate;
        s->correction+=bound(desired-s->correction,-rate*i.dt,rate*i.dt);
    }
    if(fabsf(beta)>.16f)s->transition=.12f;
    else s->transition=fmaxf(0,s->transition-i.dt);
    float engage=smooth(fabsf(beta),.04f,.16f);
    engage=fmaxf(engage,s->recovery*hands_off);
    /* Keep a recent drift engaged across centre while the chassis is rotating.
       Ordinary straight driving/cornering cannot start this short latch. */
    if(s->transition>0&&fabsf(s->yaw)>.45f&&hands_off<.5f)engage=1;
    engage*=smooth(i.forward,2,7);
    s->engagement=approach(s->engagement,engage,i.dt,engage>s->engagement?.012f:(hands_off>.5f?.02f:.06f));
    /* Do not suppress the player's input near centre: that felt like a spring.
       Manual is additive angle authority, with full raw steering at zero slip. */
    float authority=1-(1-bound(c->manual,.2f,1))*smooth(fabsf(beta),.08f,.30f)*(1-manual_takeover);
    float assisted=o.steering*authority+s->correction*(1-manual_takeover);
    o.steering=bound(o.steering+(assisted-o.steering)*s->engagement,-1,1);
    /* During a deliberate stick sweep, a brief pass through neutral can
       restore old-side assist just as the player is steering away from it.
       Rate-limit only the rebuilding portion of the additive correction. */

    float sweep=(i.stick-s->previous_stick)*side/i.dt;
    s->previous_stick=i.stick;
    s->crossing_timer=fmaxf(0,s->crossing_timer-i.dt);
    if(fabsf(beta)>.45f && sweep < -2.f)s->crossing_timer=.12f;
    float added=(o.steering-i.stick)*side;
    if(s->crossing_timer>0 && fabsf(beta)>.35f && added>0){
        float cap=fmaxf(0,s->previous_added*side)+1.5f*i.dt;
        if(added>cap)o.steering=i.stick+side*cap;
    }
    s->previous_added=o.steering-i.stick;
    o.slip=beta;o.yaw=s->yaw;o.engagement=s->engagement;return o;
}
