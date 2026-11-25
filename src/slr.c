#include "steering_sql.h"
static const char drift_kit_sql[]={
#embed "drift_kits.sql" suffix(,)
    0
};
/* FH2 1.0.0.64 / Azimuth. GPL-3.0-or-later; see THIRD-PARTY-NOTICES.md.
   AGA-inspired control law, native FH2 input adapter, no virtual controller. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "reference/fh2mod.h"
#include "assist.h"
#include "signatures.h"
#include "camera_math.h"

static const FH2ModApi *api;
static FH2Mod *mod;
static unsigned char *base;
static SRWLOCK lock=SRWLOCK_INIT;
static AssistConfig config;
static AssistState state;
static int camera_enabled=1;
static float camera_forward=.5f;
static int chinese;
static void (*original_chase[3])(void*,float,unsigned char);
static volatile LONG camera_samples,suspension_samples;
static uintptr_t last_car;
static volatile LONG samples,corrections,status_code;
static wchar_t ini[MAX_PATH];
static int readmem(const void* p,void* out,size_t n){SIZE_T got=0;return p && ReadProcessMemory(GetCurrentProcess(),p,out,n,&got) && got==n;}
static void* ptr(const void* p){void* v=0;readmem(p,&v,sizeof(v));return v;}
static int vt(void* p,unsigned rva){return ptr(p)==base+rva;}
static int readint(void* p,int *v){return readmem(p,v,4);}
static int gameplay(unsigned char* controller);
static int chinese_ui(void){
    unsigned char *game=api->game_base(),*tables=0,code[32];size_t length=0,capacity=0;
    if(game && readmem(game+0x3662990,&tables,8) && tables &&
       readmem(tables+0x30,code,32)){
        memcpy(&length,code+16,8);memcpy(&capacity,code+24,8);
        if(capacity==15&&length>=2&&length<=3&&code[length]==0)
            return !_stricmp((char*)code,"CHT")||!_stricmp((char*)code,"CHS");
    }
    /* Mods load before game text tables. Use the same launch-language choice
       that Azimuth passes into FH2, not the Windows display language. */
    wchar_t path[MAX_PATH],language[32];
    swprintf(path,MAX_PATH,L"%ls\\data\\azimuth.ini",api->azimuth_dir);
    wchar_t game_id[32],section[48];
    GetPrivateProfileStringW(L"launcher",L"game",L"",game_id,32,path);
    swprintf(section,48,L"title-%ls",game_id);
    if(!game_id[0] || !GetPrivateProfileStringW(section,L"language",L"",language,32,path))
        GetPrivateProfileStringW(L"launcher",L"language",L"en-US",language,32,path);
    return !_wcsnicmp(language,L"zh",2);
}
static uintptr_t method(void *p,unsigned slot);
static PVOID volatile trace_player,trace_confirmed;
#include "direct_rack.h"
static SRWLOCK manual_lock=SRWLOCK_INIT;
static DirectRack manual_state;
static void *manual_body;
static ULONGLONG manual_tick;
static volatile LONG reset_player_motion;
static int fresh_rack(void *body){
    /* Caller holds manual_lock. Input, wheel physics and rendering may run on different threads. */
    return body && manual_body==body && manual_state.valid &&
           InterlockedCompareExchangePointer(&trace_player,NULL,NULL)==body &&
           InterlockedCompareExchangePointer(&trace_confirmed,NULL,NULL)==body &&
           GetTickCount64()-manual_tick<=80;
}
static int player_wheel_pose(uintptr_t body);
static void clear_manual(void){
    AcquireSRWLockExclusive(&manual_lock);
    manual_body=NULL; manual_state=(DirectRack){0};
    ReleaseSRWLockExclusive(&manual_lock);
}
#include "visual_lock.h"
#include "cockpit_sync.h"
#include "wheel_lock.h"
#include "cockpit_presentation.h"
/* The native spring loader just used this same selected result row to read
   FrontSpringDamperPhysicsID and RearSpringDamperPhysicsID (FH2 1.0.0.64).
   Its integer getter takes a row index and an MSVC short UTF-8 string. */
typedef struct {char bytes[16];size_t length,capacity;} ShortField;
typedef int (*RowInt)(void*,int,const ShortField*,int);
static int row_kit_level(void *row,RowInt get){
    static const ShortField stock={"IsStock",7,15},tier={"Level",5,15};
    return get(row,0,&stock,0)?0:get(row,0,&tier,0);
}
static int installed_kit_level(FH2Context *ctx,int ordinal,int part){
    void *row=ptr((void*)(ctx->rbp+0x538));
    if(row){
        RowInt get=(RowInt)method(row,0xa0);
        return get?row_kit_level(row,get):0;
    }
    /* Some previews have only the selected part number. */
    if(ordinal<=0)return 0;
    int64_t level=(int64_t)part-(int64_t)ordinal*1000;
    return level==5 || level==6?(int)level:0;
}
static void on_suspension(FH2Context* ctx,void* user){
    (void)user;int ordinal=0,part=0;
    if(!readint((void*)(uintptr_t)(ctx->rsp+0x60),&ordinal)||
       !readint((void*)(uintptr_t)(ctx->rbp+0x93c),&part))return;
    visual_spec(ctx->rsi,ordinal,part);
    int level=installed_kit_level(ctx,ordinal,part);
    if(level==5 || level==6){
        float limit=level==6?74.f:60.f;
        float angles[2]={limit,nextafterf(limit,0.f)};SIZE_T written=0;
        if(!InterlockedCompareExchange(&lock_ready,0,0))
            angles[0]=angles[1]=60.f;
        if(WriteProcessMemory(GetCurrentProcess(),(void*)(uintptr_t)(ctx->rsi+0x5c),angles,8,&written)&&written==8)InterlockedIncrement(&suspension_samples);
    }
}
static void chase_update(void* raw,float dt,unsigned char reinit,int index){
    original_chase[index](raw,dt,reinit);
    int enabled;float distance;
    AcquireSRWLockShared(&lock);enabled=camera_enabled;distance=camera_forward;ReleaseSRWLockShared(&lock);
    if(!enabled||!isfinite(dt)||dt<=0||dt>.1f)return;
    unsigned char *camera=raw,*target=ptr(camera+8),*body=0;
    for(int depth=0;depth<3&&target&&vt(target,0x2e79540);depth++)target=ptr(target+8);
    if(!target||(!vt(target,0x2c5ce28)&&!vt(target,0x2e6b348)))return;
    body=ptr(target+0x4be8);
    if(!body||(!vt(body,0x2eb4bb8)&&!vt(body,0x2eb5e18)))return;
    unsigned char* manager=ptr(base+0x365d7c8),*controller=ptr(manager?manager+0xab28:0);
    if(!controller||!gameplay(controller))return;
    float basis[12],velocity[4],look[3],shift[3];double position[3];
    if(!readmem(body-0x380,basis,sizeof(basis))||!readmem(body-0x350,velocity,sizeof(velocity))||
       !readmem(camera+0x1b0,look,sizeof(look))||!readmem(camera+0x190,position,sizeof(position)))return;
    float speed=0;for(int n=0;n<3;n++)speed+=basis[8+n]*velocity[n];
    if(!isfinite(speed)||speed<2||basis[5]<.5f)return;
    distance*=fminf(1,(speed-2)/4);
    if(!chase_pivot_shift(basis+8,look,distance,shift))return;
    for(int n=0;n<3;n++){if(!isfinite(position[n]))return;position[n]+=shift[n];}
    memcpy(camera+0x190,position,sizeof(position));InterlockedIncrement(&camera_samples);
}
static void chase0(void* c,float dt,unsigned char r){chase_update(c,dt,r,0);}
static void chase1(void* c,float dt,unsigned char r){chase_update(c,dt,r,1);}
static void chase2(void* c,float dt,unsigned char r){chase_update(c,dt,r,2);}
static uintptr_t method(void* p,unsigned slot){
    unsigned char* table=ptr(p);uintptr_t f=(uintptr_t)ptr(table?table+slot:0);
    return f>(uintptr_t)base+0x1000 && f<(uintptr_t)base+0x1dcc000 ? f:0;
}
static int gameplay(unsigned char* controller){
    unsigned char* game=ptr(base+0x34aba60);unsigned char* sim=ptr(game?game+0x48:0);unsigned char paused=1;
    struct {void *proxy;unsigned char** blocks;size_t capacity,start,count;} q;
    if(!readmem(sim?sim+0x28:0,&paused,1)||paused||!readmem(controller+0x38,&q,sizeof(q)))return 0;
    if(!q.count||!q.blocks||!q.capacity||q.capacity>4096||(q.capacity&(q.capacity-1))||q.count>q.capacity*2||q.start>SIZE_MAX-q.count)return 0;
    unsigned char* block=ptr(q.blocks+((q.start/2)&(q.capacity-1)));unsigned permissions=0;
    return readmem(block?block+(q.start%2)*8+4:0,&permissions,4)&&(permissions&1);
}
static void reset(int reason){
    AcquireSRWLockExclusive(&manual_lock);
    InterlockedExchangePointer(&trace_player,NULL);
    InterlockedExchangePointer(&trace_confirmed,NULL);
    manual_body=NULL;manual_state=(DirectRack){0};
    ReleaseSRWLockExclusive(&manual_lock);
    memset(&state,0,sizeof(state));last_car=0;InterlockedExchange(&status_code,reason);
}
/* Preserve buttons, Y, triggers and the native shared activity flags. */
static void write_steering(unsigned char *controller,float value){
    unsigned flags=0;if(!readmem(controller+0x78,&flags,4))return;
    flags&=~0x600000u;
    if(value>0)flags|=0x430000u;else if(value<0)flags|=0x230000u;
    memcpy(controller+0xa0,&value,4);memcpy(controller+0x78,&flags,4);
}

/* The late axis contains FH2Mouse's driving keys and the physical stick.
   Probe without applying twice if a game mode skipped the previous late read. */
enum AxisPhase { AXIS_IDLE, AXIS_WAIT, AXIS_PROBE };
static enum AxisPhase keyboard_frame;
static int keyboard_hook;
static float keyboard_dt;
#ifndef FH2_INPUT_FIXTURE
static int mouse_keyboard_loaded(void){return GetModuleHandleW(L"FH2Mouse.dll")!=NULL;}
static unsigned char *acquire_local_car(void *game){
    if(!game)return NULL;
    void* player=((void*(*)(void*,int))(base+0x21e0c0))(game,0);
    if(!player)return NULL;
    unsigned char* car=0;
    ((void*(*)(void*,void**))(base+0x2da060))(player,(void**)&car);
    return car;
}
static int wheel_contacts(unsigned char *body){
    uintptr_t contact=method(body,0x4e0);
    if(contact!=(uintptr_t)(base+0x12e8040))return -1;
    int grounded=0;
    for(int n=0;n<4;n++)grounded+=((void*(*)(void*,int))contact)(body,n)!=NULL;
    return grounded;
}
#endif
static float correct_steering(float stick,float dt,AssistConfig c,void *expected_car){
    unsigned char *car=acquire_local_car(ptr(base+0x34aba60));
    if(!car){reset(4);return stick;}
    uintptr_t release=method(car,0x18);
    if(expected_car){
        enum AxisPhase phase=keyboard_frame;
        if(car==expected_car)keyboard_frame=AXIS_IDLE;
        if(car!=expected_car || phase!=AXIS_WAIT){
            if(release)((void(*)(void*))release)(car);
            return stick;
        }
    }
    int mode=-1;
    unsigned char* body=0;
    int valid=vt(car,0x2c5cb80)&&readint(car+0x4d30,&mode)&&mode==1;
    if(valid)body=ptr(car+0x4c08);
    valid=valid&&body&&(vt(body,0x2eb4bb8)||vt(body,0x2eb5e18));
    float basis[12]={0},velocity[4]={0};int grounded=0;
    if(valid){
        valid=readmem(body-0x380,basis,sizeof(basis))&&readmem(body-0x350,velocity,sizeof(velocity));
        if(valid){grounded=wheel_contacts(body);valid=grounded>=0;}
    }
    if(valid){
        float lateral=0,forward=0,nx=0,nz=0;
        for(int n=0;n<3;n++){lateral+=velocity[n]*basis[n];forward+=velocity[n]*basis[8+n];nx+=basis[n]*basis[n];nz+=basis[8+n]*basis[8+n];}
        valid=isfinite(nx)&&isfinite(nz)&&nx>.95f&&nx<1.05f&&nz>.95f&&nz<1.05f&&isfinite(basis[5]);
        if(valid){
            AcquireSRWLockExclusive(&manual_lock);
            if(InterlockedExchangePointer(&trace_player,body)!=body){
                InterlockedExchangePointer(&trace_confirmed,NULL);
                manual_body=NULL;manual_state=(DirectRack){0};
                InterlockedExchange(&reset_player_motion,1);
            }
            if(InterlockedExchange(&reset_player_motion,0) || last_car!=(uintptr_t)car)
                memset(&state,0,sizeof(state));
            last_car=(uintptr_t)car;
            int supported=grounded>=2 && basis[5]>.45f;
            AssistInput in={stick,lateral,forward,atan2f(basis[8],basis[10]),dt,supported,1};
            AssistOutput out=assist_step(&state,&c,in);
            /* Confirm player dynamics by matching the actual wheel solver instance. */
            int direct=0;
            float rack_command=0,rack_target=0,limit=0,measured=0;
            RackGeometry geometry={0};
            float neutral=0,hand=0,front_flow=0,window_mix=0;
            if(c.enabled && c.strength>0 &&
               isfinite(forward) && isfinite(lateral) && isfinite(out.yaw) &&
               InterlockedCompareExchangePointer(&trace_confirmed,NULL,NULL)==body){
                WheelBinding *binding=*wheel_binding((uintptr_t)body);
                if(binding){
                    limit=binding->limit;
                    unsigned mm=(unsigned)InterlockedCompareExchange(&binding->geometry_mm,0,0);
                    geometry=(RackGeometry){.001f*(mm&0xffff),.001f*(mm>>16)};
                }
            }
            if(limit>0 && dt>0 && dt<=.1f && readmem(body+8,&measured,4) && isfinite(measured)){
                ULONGLONG tick=GetTickCount64();
                if(manual_body!=body || !manual_state.valid || tick-manual_tick>80){
                    /* A resumed or reused vehicle needs a new completed wheel step. */
                    (*wheel_binding((uintptr_t)body))->cockpit.valid=0;
                }
                if(manual_body!=body || tick-manual_tick>100)manual_state=(DirectRack){0};
                manual_body=body; manual_tick=tick; cockpit_player_car=car;
                if(supported)
                    rack_update(&manual_state,stick,forward,lateral,out.yaw,state.slip_rate,geometry,limit,dt,measured*57.29578f,c);
                else
                    rack_unsupported(&manual_state,stick,limit,dt,measured*57.29578f,c,hypotf(lateral,forward));
                neutral=manual_state.neutral;hand=manual_state.hand;
                front_flow=manual_state.front_flow;window_mix=manual_state.window_mix;
                rack_command=manual_state.angle;rack_target=manual_state.target;direct=1;
            }else{manual_body=NULL;manual_state=(DirectRack){0};}
            ReleaseSRWLockExclusive(&manual_lock);
            static ULONGLONG next_trace;
            static unsigned trace_count;
            ULONGLONG now=GetTickCount64();
            static ULONGLONG parked_until;
            static unsigned parked_count;
            int low_speed = hypotf(forward,lateral)<2.5f;
            if(low_speed && fabsf(stick)>.10f)parked_until=now+1500;
            int parked_sample = low_speed && now<parked_until && parked_count<8000;
            if(now>=next_trace && (parked_sample || (trace_count<6000 && hypotf(forward,lateral)>2))){
                next_trace=now+30;
                float demand=0,left=0,right=0;
                int ok=InterlockedCompareExchangePointer(&trace_confirmed,NULL,NULL)==body &&
                    readmem(body+8,&demand,4) && readmem(body+0x11c8,&left,4) &&
                    readmem(body+0x1678,&right,4);
                char line[480];
                float body_slip=atan2f(lateral,forward)*57.29578f;
                snprintf(line,sizeof(line),"SteerTrace ms=%llu input=%s stick=%.3f out=%.3f slipDeg=%.2f yaw=%.3f holdDeg=%.2f closing=%.3f speed=%.1f ground=%d wheelValid=%d demandDeg=%.2f leftDeg=%.2f rightDeg=%.2f direct=%d targetDeg=%.2f commandDeg=%.2f autoDeg=%.2f handDeg=%.2f frontFlow=%.2f windowMix=%.3f supported=%d limitDeg=%.2f wheelbase=%.3f track=%.3f",
                    (unsigned long long)now,expected_car?"merged":"pad",stick,direct?stick:out.steering,body_slip,out.yaw,state.hold_angle*57.29578f,
                    body_slip>=0?-state.slip_rate:state.slip_rate,hypotf(forward,lateral),grounded,ok,demand*57.29578f,left*57.29578f,right*57.29578f,direct,rack_target,rack_command,neutral,hand,front_flow,window_mix,supported,limit,geometry.wheelbase,geometry.track);
                api->log(mod,line);
                if(parked_sample)++parked_count;else ++trace_count;
            }
            if(!direct && isfinite(out.steering)&&fabsf(out.steering-stick)>.00001f){
                stick=out.steering;
                InterlockedIncrement(&corrections);
            }
            InterlockedExchange(&status_code,grounded<2?6:forward<2?7:out.engagement>.1f?9:8);
        }
    }
    if(!valid)reset(5);
    if(release)((void(*)(void*))release)(car);
    return stick;
}
static int controller_slot(unsigned char *manager,unsigned char *input){
    uintptr_t first=(uintptr_t)manager+0x10, selected=(uintptr_t)input;
    return selected>=first && selected<first+8*0x1300 && (selected-first)%0x1300==0;
}
static void on_input(FH2Context* ctx,void* user){
    (void)user;
    enum AxisPhase previous=keyboard_frame;
    keyboard_frame=AXIS_IDLE;
    unsigned char* manager=ptr(base+0x365d7c8);
    if(!manager||!vt(manager,0x2e7a2e8)){reset(2);return;}
    unsigned char* controller=ptr(manager+0xab28);
    if(controller && !controller_slot(manager,controller)){reset(2);return;}
    InterlockedIncrement(&samples);
    unsigned char connected=0,type=0;
    AssistConfig c;AcquireSRWLockShared(&lock);c=config;ReleaseSRWLockShared(&lock);
    if(!c.enabled){reset(1);return;}
    if(controller && (!readmem(controller+4,&connected,1)||!readmem(controller+6,&type,1)||type==2||type==3)){reset(2);return;}
    float dt=ctx->xmm[12].f[0],stick=0;
    if(!isfinite(dt)||dt<=0||dt>.1f){reset(3);return;}
    if(keyboard_hook && mouse_keyboard_loaded()){
        int has_pad=connected==1 && ptr(controller+0x60)!=NULL;
        if(!has_pad && previous==AXIS_WAIT)reset(2);
        keyboard_dt=dt;
        keyboard_frame=(!has_pad || previous==AXIS_IDLE)?AXIS_WAIT:AXIS_PROBE;
        if(keyboard_frame==AXIS_WAIT)return;
    }
    if(connected!=1){reset(2);return;}
    if(!gameplay(controller)){reset(2);return;}
    if(!readmem(controller+0xa0,&stick,4)||!isfinite(stick)||fabsf(stick)>1.01f){reset(3);return;}
    float result=correct_steering(stick,dt,c,NULL);
    if(result!=stick){
        write_steering(controller,result);
        unsigned char *merged=manager+0x9810;float merged_x;
        if(readmem(merged+0xa0,&merged_x,4)&&fabsf(merged_x-stick)<.00001f)
            write_steering(merged,result);
    }
}

/* FH2Mouse supplies the player's remapped keys in GetAnalog. Consume its
   final axis here, immediately before FH2 stores it for the car. */
static void on_keyboard_axis(FH2Context* ctx,void* user){
    (void)user;
    if(!keyboard_frame)return;
    unsigned char *manager=ptr(base+0x365d7c8),*input=(void*)(uintptr_t)ctx->rdi;
    if(!manager || !vt(manager,0x2e7a2e8) || !input ||
       (input!=manager+0x9810 && !controller_slot(manager,input)))return;
    unsigned char type=0;
    if(!readmem(input+6,&type,1) || type==2 || type==3)return;
    void *car=ptr((void*)(uintptr_t)ctx->rsi);
    if(!car || !vt(car,0x2c5cb80))return;
    if(!gameplay(input))return;
    float raw=ctx->xmm[0].f[0];
    if(!isfinite(raw)||fabsf(raw)>1.01f)return;
    AssistConfig c;AcquireSRWLockShared(&lock);c=config;ReleaseSRWLockShared(&lock);
    if(!c.enabled)return;
    ctx->xmm[0].f[0]=correct_steering(raw,keyboard_dt,c,car);
}
#include "steering_menu.h"
static DWORD WINAPI setup(void* unused){
    (void)unused;base=api->wait_for_code(INFINITE);if(!base)return 0;
    for(unsigned i=0;i<sizeof(signatures)/sizeof(signatures[0]);i++){
        unsigned char b[16];if(!readmem(base+signatures[i].rva,b,16)||memcmp(b,signatures[i].bytes,16)){
            api->log(mod,"Unsupported/modified game code: assist not installed.");return 0;
        }
    }
    FH2Hook* hook=api->hook_instruction(mod,"Gamepad after completed native aggregation",base+0x1075fc3,on_input,0);
    if(!hook){api->log(mod,"Input hook rejected; original controls retained.");return 0;}
    if(!api->enable_hooks(mod,hook)){api->log(mod,"Input hook could not be enabled; original controls retained.");return 0;}
    static const unsigned char axis_bytes[]={0xe8,0x85,0x14,0xd7,0x00,0xc5,0xfa,0x11,0x44,0x24,0x28,0x80,0x7f,0x06,0x02};
    if(!memcmp(base+0x2fdbb6,axis_bytes,sizeof(axis_bytes))){
        FH2Hook* axis=api->hook_instruction(mod,"Keyboard driving steering after GetAnalog",base+0x2fdbbb,on_keyboard_axis,0);
        keyboard_hook=axis && api->enable_hooks(mod,axis);
    }
    api->log(mod,keyboard_hook?"Keyboard steering adapter ready.":"Keyboard steering adapter unavailable.");
    static const unsigned tables[]={0x2e783a0,0x2e78610,0x2e78880};
    void* callbacks[]={(void*)chase0,(void*)chase1,(void*)chase2};
    for(int i=0;i<3;i++){
        if(ptr(base+tables[i]+0x20)!=base+0x103c820){api->log(mod,"Chase camera slot already modified; retained existing camera for this view.");continue;}
        FH2Hook* h=api->hook_pointer(mod,"Forward-pivot chase camera",(void**)(base+tables[i]+0x20),callbacks[i],(void**)&original_chase[i]);
        if(!h||!api->enable_hooks(mod,h))api->log(mod,"Chase camera hook unavailable for one view; native camera retained.");
    }
    /* Both supported dynamics classes release their steering interface at +0x3a0. */
    static const unsigned char destroy_a[]={0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe};
    static const unsigned char destroy_b[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48};
    if(!memcmp(base+0x12af230,destroy_a,sizeof(destroy_a)) &&
       !memcmp(base+0x12aefe0,destroy_b,sizeof(destroy_b))){
        FH2Hook *a=api->hook_instruction(mod,"Release vehicle steering A",base+0x12af230,unbind_wheels,0);
        FH2Hook *b=api->hook_instruction(mod,"Release vehicle steering B",base+0x12aefe0,unbind_wheels,0);
        if(!a || !b || !api->enable_hooks(mod,a) || !api->enable_hooks(mod,b))
            api->log(mod,"Vehicle release tracking limited; bindings refresh on assembly.");
    }
    if(!memcmp(base+0x12e3da9,init_bytes,sizeof(init_bytes)) &&
       !memcmp(base+0x12bab1f,angle_bytes,sizeof(angle_bytes))){
        FH2Hook *bind=api->hook_instruction(mod,"Vehicle steering specification",base+0x12e3da9,bind_wheels,0);
        FH2Hook *angles=api->hook_instruction(mod,"Player final front wheel angles",base+0x12bab1f,set_wheel_angles,0);
        if(bind && angles && api->enable_hooks(mod,bind) && api->enable_hooks(mod,angles))InterlockedExchange(&lock_ready,1);
    }
    api->log(mod,lock_ready?"Player front-wheel assist enabled for all standard vehicle specifications.":"Player front-wheel assist unavailable; input fallback active.");
    static const unsigned char visual_bytes[]={0xc5,0xf8,0x28,0x48,0x10,0xc5,0xf8,0x28,0x0,0x48,0x8b,0xbc,0x24,0x0,0x1,0x0};
    if(!memcmp(base+0xf582c7,visual_bytes,16)){
        FH2Hook *h=api->hook_instruction(mod,"Render-only wheel steering",base+0xf582c7,visual_wheel,0);
        if(h && api->enable_hooks(mod,h)){
            static const unsigned char route_bytes[]={0x0f,0x84,0xc1,0x01,0,0};
            if(!memcmp(base+0xf7f500,route_bytes,sizeof(route_bytes))){
                FH2Hook *route=api->hook_instruction(mod,"Selected suspension visual route",base+0xf7f500,visual_route,0);
                api->log(mod,(route && api->enable_hooks(mod,route))?"Render-only steering v7: animated branch routing enabled.":"Animated branch routing unavailable.");
            }
        }else api->log(mod,"Render-only wheel steering unavailable.");
    }
    FH2Hook* suspension=api->hook_instruction(mod,"FD suspension steering only when equipped",base+0x10577d5,on_suspension,0);
    if(!suspension||!api->enable_hooks(mod,suspension))api->log(mod,"FD steering hook unavailable; native steering angles retained.");
    install_cockpit_sync();
    LONG previous=-1;unsigned elapsed=0;
    for(;;){
        Sleep(1000);retry_save();
        if(++elapsed%10==0){LONG status=InterlockedCompareExchange(&status_code,0,0);
            if(status!=previous){char line[300];snprintf(line,sizeof(line),"State=%ld (1 off,2 menu/controller,3 timing,4 no car,5 telemetry mismatch,6 airborne,7 low/reverse,8 ready,9 correcting); samples=%ld corrections=%ld camera=%ld FD_loads=%ld",status,InterlockedCompareExchange(&samples,0,0),InterlockedCompareExchange(&corrections,0,0),InterlockedCompareExchange(&camera_samples,0,0),InterlockedCompareExchange(&suspension_samples,0,0));api->log(mod,line);previous=status;}
        }
    }
}
FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* a){
    if(!a||a->api_version<7||a->size<offsetof(FH2ModApi,hook_instruction)+sizeof(a->hook_instruction))return 1;
    api=a;
    int supports_catalog=a->api_version>=10 && a->size>=offsetof(FH2ModApi,tr)+sizeof(a->tr) &&
                         a->add_translation && a->tr;
    chinese=supports_catalog?0:chinese_ui();
    FH2ModInfo info={0};
    info.size=sizeof(info);info.name="FH2 Drift Assist";info.version="0.5.2-arcade1";info.platform_version="2.6.0";
    info.description=menu_text(&Description);
    mod=api->register_mod(&info);if(!mod)return 1;
    if(supports_catalog){
        native_translation=a->add_translation(mod,"zh-Hant",traditional_catalog,sizeof(traditional_catalog)-1)>0;
        if(!native_translation)chinese=chinese_ui();
        if(a->set_mod_description)a->set_mod_description(mod,menu_text(&Description));
    }
    if(!api->add_sql)return 1;
    api->add_sql(mod,STEERING_RATE_SQL);
    api->add_sql(mod,drift_kit_sql);
    if(api->log)api->log(mod,"Queued platform-managed Polestar steering rates: turning=720, straighten=720, countersteer=900. Check database apply log for success.");
    swprintf(ini,MAX_PATH,L"%ls\\fh2-mods\\FH2DriftAssist.ini",api->azimuth_dir);
    register_settings();
    HANDLE thread=CreateThread(0,0,setup,0,0,0);if(!thread)return 1;CloseHandle(thread);return 0;
}
