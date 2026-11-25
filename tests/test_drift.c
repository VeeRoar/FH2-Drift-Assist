#include "../src/assist.h"
#include "../src/direct_rack.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static AssistConfig settings;

static void sample(DirectRack *rack,float dt,float stick,float speed,float slip,float yaw,float rate,AssistConfig cfg){
    float radians=slip*.0174532925f;
    rack_update(rack,stick,speed*cosf(radians),speed*sinf(radians),yaw,rate,(RackGeometry){2.6f,1.6f},74,dt,0,cfg);
    assert(isfinite(rack->angle) && fabsf(rack->angle)<=74.001f);
}
static float steady(float stick,float speed,float slip,float yaw,float rate,AssistConfig cfg,int hz){
    DirectRack rack={0};
    for(int i=0;i<hz*4;i++)sample(&rack,1.f/hz,stick,speed,slip,yaw,rate,cfg);
    return rack.angle;
}
static void check_tuning(void){
    AssistConfig slow=settings,fast=settings;
    slow.steering_speed=.5f;fast.steering_speed=1.5f;
    DirectRack a={0},b={0};
    for(int i=0;i<3;i++){sample(&a,1.f/60,1,0,0,0,0,slow);sample(&b,1.f/60,1,0,0,0,0,fast);}
    assert(b.angle>a.angle*2 && a.target==b.target);
    a.angle=b.angle=60;
    slow.release_speed=.5f;fast.release_speed=1.5f;
    sample(&a,1.f/60,0,0,0,0,0,slow);sample(&b,1.f/60,0,0,0,0,0,fast);
    assert(60-b.angle>2*(60-a.angle) && a.angle<60 && b.angle>0 && a.target==0 && b.target==0);
    /* Released response follows the current drift balance, including in the air. */
    a=b=(DirectRack){.angle=60,.neutral=30,.valid=1,.left_range=20,.right_range=20};
    rack_unsupported(&a,0,74,1.f/60,0,slow,20);rack_unsupported(&b,0,74,1.f/60,0,fast,20);
    assert(a.target==30 && b.target==30 && b.angle<a.angle && b.angle>=30);
    slow=settings;fast=settings;slow.front_slip_deg=4;fast.front_slip_deg=24;
    assert(fabsf(steady(1,25,0,0,0,slow,60)-4)<.01f);
    assert(fabsf(steady(1,25,0,0,0,fast,60)-24)<.01f);
    slow=settings;fast=settings;slow.deadzone=0;fast.deadzone=.15f;
    assert(steady(.08f,0,0,0,0,slow,60)>5 && steady(.08f,0,0,0,0,fast,60)==0);
    assert(fabsf(steady(-1,0,0,0,0,slow,60)-steady(-1,0,0,0,0,fast,60))<.0001f);
    puts("PASS response speed, released countersteer, in-air hold, slip window, deadzone and full steering-key travel");
}
static void scenarios(int hz){
    float dt=1.f/hz;
    DirectRack parked={0};
    for(int i=0;i<4*hz;i++)sample(&parked,dt,1,0,0,0,0,settings);
    assert(parked.angle>73.99f);
    int frames=0;
    while(parked.angle>0 && frames<4*hz){
        float before=parked.angle;
        sample(&parked,dt,-1,0,0,0,0,settings);frames++;
        assert(parked.angle<=before+.001f);
    }
    assert(parked.angle<=0 && frames*dt>=.30f && frames*dt<=.40f);
    for(int i=0;i<4*hz;i++)sample(&parked,dt,0,0,0,0,0,settings);
    assert(fabsf(parked.angle)<.001f);
    float growing=steady(0,20,40,-.8f,1,settings,hz),shrinking=steady(0,20,40,-.8f,-1,settings,hz);
    assert(growing>shrinking+10 && shrinking>0);
    float catch_more=steady(1,20,40,-.8f,1,settings,hz)-growing;
    float catch_less=steady(1,20,40,-.8f,-1,settings,hz)-shrinking;
    assert(catch_more>=0 && catch_less>0 && catch_less<=7.4f && growing+catch_more>shrinking+catch_less);
    assert(fabsf(shrinking+steady(0,20,-40,.8f,1,settings,hz))<.01f);
    assert(fabsf(steady(1,25,0,0,0,settings,hz)-12)<.01f);
    assert(fabsf(steady(1,6,0,0,0,settings,hz)-74)<.01f);
    assert(fabsf(steady(1,7.5f,0,0,0,settings,hz)-58.5f)<.01f);
    assert(fabsf(steady(0,25,0,0,0,settings,hz))<.01f);
    assert(steady(-1,12,150,0,0,settings,hz)<-73.99f);
    /* The installed arcade1 control law produces 38.3511 degrees here
       at 30, 60 and 120 Hz as forward travel approaches zero. */
    assert(fabsf(steady(0,20,88,0,0,settings,hz)-38.3511f)<.01f);
    /* Independent calculation of the actual left and right wheel angles
       after the solver's Ackermann split, tested for different held inputs. */
    for(int direction=-1;direction<=1;direction+=2){
        float beta=direction*40.f*.01745329252f,yaw=-direction*.8f;
        float front_x=20*sinf(beta)+yaw*1.3f;
        float left_flow=atan2f(front_x,20*cosf(beta)+.8f*yaw)*57.29578f;
        float right_flow=atan2f(front_x,20*cosf(beta)-.8f*yaw)*57.29578f;
        for(int i=-4;i<=4;i++){
            float rack=steady(.25f*i,20,40.f*direction,yaw,0,settings,hz);
            float a=fabsf(rack)*.01745329252f;
            float outer=copysignf(atan2f(sinf(a),cosf(a)+(.25f*1.6f/2.6f)*sinf(a))*57.29578f,rack);
            assert(fabsf((rack<0?rack:outer)-left_flow)<12.01f);
            assert(fabsf((rack>0?rack:outer)-right_flow)<12.01f);
        }
    }
    DirectRack transition={0};
    for(int i=0;i<hz;i++)sample(&transition,dt,0,22,40,0,0,settings);
    for(int i=1;i<=hz/2;i++){
        float slip=40-160*i*dt;
        sample(&transition,dt,0,22,slip,0,-2.79f,settings);
        assert(transition.neutral*slip>=-.001f);
    }
    assert(transition.angle<-25);
    AssistConfig cfg=settings;cfg.max_correction=0;
    assert(steady(-1,15,40,0,0,cfg,hz)<-73.99f);
    cfg=settings;cfg.max_correction=.4f;
    assert(steady(0,20,60,0,0,cfg,hz)<=74*.4f+.001f);
    cfg=settings;cfg.manual=.2f;
    assert(steady(1,20,40,0,0,cfg,hz)<steady(1,20,40,0,0,settings,hz)-5);
    printf("PASS %d Hz: parked lock reversal crosses centre in %.0f ms; drift catch, unwind, transition, reverse, sliders\n",hz,1000*frames*dt);
}
static double number(const char *line,const char *field){
    const char *p=strstr(line,field);assert(p);return strtod(p+strlen(field),NULL);
}
/* Replay uses 2.60 m wheelbase and 1.60 m track; the historical trace did
   not record geometry. As in the fixture the left wheel is outside on right lock. */
static float replay_left_wheel(float command){
    if(command<=0)return command;
    float a=command*.01745329252f;
    return atan2f(sinf(a),cosf(a)+.25f*1.6f/2.6f*sinf(a))*57.29578f;
}
static void replay(const char *path){
    FILE *log=fopen(path,"rb");assert(log);
    char line[1400];DirectRack rack={0};double previous=0;
    int full=0,old_lock=0,new_lock=0;double old_sum=0,new_sum=0;
    int airborne=0,edges=0,quiet_edges=0,was_supported=0;
    float previous_stick=0,previous_wheel=0,old_largest=0,new_largest=0;
    while(fgets(line,sizeof(line),log)){
        if(!strstr(line,"SteerTrace "))continue;
        double ms=number(line," ms="),elapsed=(ms-previous)*.001;
        if(previous==0 || elapsed<=0 || elapsed>.1){rack=(DirectRack){0};was_supported=0;elapsed=1./60;}
        previous=ms;
        int supported=number(line," ground=")>=2;
        if(!number(line," wheelValid=") || (!number(line," direct=") && (supported || !rack.valid))){
            rack=(DirectRack){0};was_supported=0;continue;
        }
        float slip=number(line," slipDeg="),yaw=number(line," yaw="),speed=number(line," speed=");
        float rate=number(line," closing=")*(slip>=0?-1:1),stick=number(line," stick=");
        int steps=(int)ceil(elapsed*120);
        float before=rack.angle,left=number(line," leftDeg=");
        for(int i=0;i<steps;i++){
            if(supported)sample(&rack,(float)(elapsed/steps),stick,speed,slip,yaw,rate,settings);
            else rack_unsupported(&rack,stick,74,(float)(elapsed/steps),before,settings,speed);
        }
        if(!supported){
            airborne++;
            if(was_supported){
                edges++;
                if(fabsf(stick)<.04f && fabsf(previous_stick)<.04f){
                    quiet_edges++;
                    old_largest=fmaxf(old_largest,fabsf(left-previous_wheel));
                    new_largest=fmaxf(new_largest,fabsf(replay_left_wheel(rack.angle)-replay_left_wheel(before)));
                }
            }
        }
        was_supported=supported;previous_stick=stick;previous_wheel=left;
        if(supported && fabsf(slip)>=20 && fabsf(stick)>=.95f){
            float old=(float)fabs(number(line," targetDeg=")),now=fabsf(rack.target);
            full++;old_lock+=old>73.5f;new_lock+=now>73.5f;old_sum+=old;new_sum+=now;
        }
    }
    fclose(log);assert(full>0);
    printf("Recorded high-angle full-input samples %d. Old/new targets near lock %d/%d. Mean absolute target %.1f/%.1f degrees.\n",full,old_lock,new_lock,old_sum/full,new_sum/full);
    printf("Replayed %d low-contact samples, %d takeoffs (%d centered stick). Largest left-wheel step on centered takeoffs, measured old %.2f deg / modeled new %.2f deg.\n",airborne,edges,quiet_edges,old_largest,new_largest);
}
int main(int argc,char **argv){
    settings=assist_defaults();
    check_tuning();
    scenarios(30);scenarios(60);scenarios(120);
    if(argc==2)replay(argv[1]);
    return 0;
}
