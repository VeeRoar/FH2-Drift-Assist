/* Offline fixture: only game-owned car lookup, contact lookup and module
   presence are substituted. Both input callbacks and wheel solver run as built. */
#include <assert.h>
#include <stdlib.h>
static unsigned char *car, *body;
static int mouse_loaded=1, contacts=4;
static int mouse_keyboard_loaded(void){return mouse_loaded;}
static unsigned char *acquire_local_car(void *game){return game?car:NULL;}
static int wheel_contacts(unsigned char *instance){return instance==body?contacts:-1;}
#define FH2_INPUT_FIXTURE
#include "../src/slr.c"

static void quiet(FH2Mod *m,const char *s){(void)m;(void)s;}
static void putp(void *address,void *value){memcpy(address,&value,sizeof(value));}
static void putf(void *address,float value){memcpy(address,&value,sizeof(value));}
static unsigned char *manager,*pad,*sim;
static FH2Context early,axis,solver;
static float wheels[2];
static void late(float value, unsigned char **vehicle){
    axis.rsi=(uintptr_t)vehicle;axis.xmm[0].f[0]=value;
    on_keyboard_axis(&axis,NULL);
}
static void solve(void){
    set_wheel_angles(&solver,NULL);
    memcpy(&wheels[0],body+0x11c8,4);
    memcpy(&wheels[1],body+0x1678,4);
}
static void frames(int count,float stick,float merged){
    for(int i=0;i<count;i++){
        putf(pad+0xa0,stick);
        on_input(&early,NULL);late(merged,&car);solve();
    }
}
static void assemble_car(unsigned char *instance,int id,int level,float limit,float filtered){
    unsigned char spec[128]={0};
    putf(spec+0x5c,limit);
    putf(spec+0x60,filtered);
    if(id){
        unsigned char stack[128]={0},frame[0x950]={0};
        int part=id*1000+level;
        memcpy(stack+0x60,&id,4);memcpy(frame+0x93c,&part,4);
        FH2Context suspension={0};
        suspension.rsp=(uintptr_t)stack;suspension.rbp=(uintptr_t)frame;
        suspension.rsi=(uintptr_t)spec;
        on_suspension(&suspension,NULL);
    }
    FH2Context bind={0};
    bind.rdi=(uintptr_t)spec;bind.rsi=(uintptr_t)instance-0x3a0;
    bind_wheels(&bind,NULL);
}
static void assemble(unsigned char *instance,float limit,float filtered){
    assemble_car(instance,0,0,limit,filtered);
}
typedef struct {void **vtable;int stock,level;} TestUpgrade;
static int column_value(void *raw,int index,const ShortField *name,int unused){
    TestUpgrade *row=raw;
    assert(index==0 && unused==0 && name->capacity==15 && strlen(name->bytes)==name->length);
    if(!strcmp(name->bytes,"IsStock"))return row->stock;
    assert(!strcmp(name->bytes,"Level"));return row->level;
}
static void check_equipped_row(void){
    void *methods[21]={0};methods[20]=(void*)column_value;
    TestUpgrade row={methods,0,6};
    unsigned char frame[0x950]={0},stack[128]={0},spec[128]={0};
    FH2Context ctx={0};ctx.rbp=(uintptr_t)frame;ctx.rsp=(uintptr_t)stack;ctx.rsi=(uintptr_t)spec;
    putp(frame+0x538,&row);
    int car_id=93500,part=1234;
    memcpy(stack+0x60,&car_id,4);memcpy(frame+0x93c,&part,4);
    unsigned char *old_base=base;
    base=(unsigned char*)((uintptr_t)column_value-0x2000);
    for(int level=5;level<=6;level++){
        row.level=level;on_suspension(&ctx,NULL);
        float radians=level==5?60.f:74.f;
        assert(*(float*)(spec+0x5c)==radians && *(float*)(spec+0x60)==nextafterf(radians,0));
    }
    /* A stock or race part may own the canonical-looking SLR ID. */
    part=car_id*1000+6;memcpy(frame+0x93c,&part,4);putf(spec+0x5c,42);
    row.stock=1;on_suspension(&ctx,NULL);assert(*(float*)(spec+0x5c)==42);
    row.stock=0;row.level=3;on_suspension(&ctx,NULL);assert(*(float*)(spec+0x5c)==42);
    base=old_base;
    puts("PASS actual equipped tier, nonstandard part number and native part using a kit-like ID");
}
static void expect_native(void){
    /* Distinct nonzero angles in both physics channels reveal accidental writes. */
    const float native[]={.13f,-.21f,.29f,-.37f};
    memcpy(body+0x11c8,native,8);
    memcpy(body+0x1678,native+2,8);
    solve();
    assert(!memcmp(body+0x11c8,native,8) && !memcmp(body+0x1678,native+2,8));
}
static void verify_stops(int id,int level,float limit,float filtered){
    const float rad=.01745329252f;
    assemble_car(body,id,level,limit,filtered);
    assert(!manual_state.valid && !!player_wheel_pose((uintptr_t)body)==(level==5 || level==6));
    if(level!=5 && level!=6)expect_native();
    for(int side=-1;side<=1;side+=2){
        frames(30,0,(float)side);
        assert(manual_state.valid && fabsf(manual_state.angle-side*limit)<.01f);
        assert(fabsf(fmaxf(wheels[0]*side,wheels[1]*side)-rad*limit)<.001f);
        assert(wheels[0]*side>0 && wheels[1]*side>0);
        assert(!memcmp(body+0x11c8,body+0x11cc,4) && !memcmp(body+0x1678,body+0x167c,4));
    }
    putf(body-0x350,7);putf(body-0x350+8,16);
    frames(60,0,0);
    assert(manual_state.neutral>20 && wheels[0]>.3f && wheels[1]>.3f);
    putf(body-0x350,0);putf(body-0x350+8,0);
}
int main(int argc,char **argv){
    static FH2ModApi fixture_api;fixture_api.log=quiet;api=&fixture_api;
    base=calloc(1,0x3700000);manager=calloc(1,0xac00);car=calloc(1,0x5000);
    unsigned char *allocation=calloc(1,0x2400),*game=calloc(1,0x100);
    sim=calloc(1,0x100);assert(base&&manager&&car&&allocation&&game&&sim);
    pad=manager+0x10;body=allocation+0x400;
    putp(base+0x365d7c8,manager);putp(manager,base+0x2e7a2e8);putp(manager+0xab28,pad);
    putp(base+0x34aba60,game);putp(game+0x48,sim);
    unsigned char permission[16]={0};permission[4]=1;void *blocks[]={permission};
    struct {void* proxy;void** blocks;size_t capacity,start,count;} queue={0,blocks,1,0,1};
    memcpy(pad+0x38,&queue,sizeof(queue));
    putp(car,base+0x2c5cb80);car[0x4d30]=1;putp(car+0x4c08,body);putp(body,base+0x2eb4bb8);
    float basis[]={1,0,0,0,0,1,0,0,0,0,1,0};memcpy(body-0x380,basis,sizeof(basis));
    body[0x11c8+0x39c]=body[0x1678+0x39c]=1;
    putf(body+0x11c8+0x468,-.8f);putf(body+0x1678+0x468,.8f);
    assemble(body,74,SLR_FILTERED_LOCK);
    early.xmm[12].f[0]=1.f/60;axis.rdi=(uintptr_t)pad;solver.rdi=(uintptr_t)body;solver.xmm[8].f[0]=2.6f;
    config=assist_defaults();keyboard_hook=1;lock_ready=1;check_equipped_row();

    frames(30,0,-1);assert(wheels[0]<-1 && wheels[1]<-1);
    frames(30,0,1);assert(wheels[0]>1 && wheels[1]>1);
    frames(60,0,0);assert(fabsf(wheels[0])<.001f && fabsf(wheels[1])<.001f);
    putp(manager+0xab28,NULL);frames(30,0,-1);assert(wheels[0]<-1 && wheels[1]<-1);
    puts("PASS keyboard alone: left, reversal, release and no selected pad");

    putp(manager+0xab28,pad);pad[4]=1;putp(pad+0x60,game);
    frames(30,0,1);assert(wheels[0]>1 && wheels[1]>1);
    frames(30,-.3f,.7f);assert(manual_state.target>45 && manual_state.target<55);
    frames(30,-.3f,-.3f);assert(manual_state.target<-18 && manual_state.target>-22);
    on_input(&early,NULL);unsigned char *other=calloc(1,0x5000);assert(other);putp(other,base+0x2c5cb80);
    late(1,&other);assert(keyboard_frame==AXIS_WAIT);late(-1,&car);
    float once=manual_state.angle;late(1,&car);assert(manual_state.angle==once && keyboard_frame==AXIS_IDLE);
    puts("PASS mixed input: centered pad, combined axis, analog takeover, foreign car and duplicate reads");

    putf(pad+0xa0,.5f);on_input(&early,NULL);on_input(&early,NULL);
    assert(keyboard_frame==AXIS_PROBE && manual_state.target>30);
    once=manual_state.angle;late(-1,&car);assert(manual_state.angle==once);
    frames(30,0,-1);assert(wheels[0]<-1 && wheels[1]<-1);
    mouse_loaded=0;frames(30,.5f,0);assert(manual_state.target>30 && keyboard_frame==AXIS_IDLE);
    puts("PASS physical pad fallback and recovery of merged input");

    /* Cross the full path from remapped axis to both wheel channels with a
       fixed 40-degree slide; the key remains held throughout each interval. */
    config=assist_defaults();mouse_loaded=1;
    putf(body-0x350,12.856f);putf(body-0x350+8,15.321f);
    frames(90,0,0);float aligned=manual_state.target;
    assert(aligned>38 && aligned<42 && wheels[0]>.5f && wheels[1]>.5f);
    frames(60,0,-1);assert(manual_state.target>28 && manual_state.target<31 && wheels[0]>.48f && wheels[1]>.48f);
    frames(60,0,1);assert(manual_state.target>50 && manual_state.target<55 && wheels[0]>.72f && wheels[1]>.72f);
    frames(60,0,0);assert(fabsf(manual_state.target-aligned)<.01f);
    putf(body-0x350,-12.856f);frames(90,0,0);assert(fabsf(manual_state.target+aligned)<.05f && wheels[0]<-.5f && wheels[1]<-.5f);
    puts("PASS 40-degree drift: unassisted stick at zero, held key both directions, release and mirrored slide reach both wheels");

    mouse_loaded=1;
    float takeoff=manual_state.angle,left_at_takeoff=wheels[0],right_at_takeoff=wheels[1];
    const int bumps[]={2,1,0,1,2,1,0,3,2,4};
    for(unsigned i=0;i<sizeof(bumps)/sizeof(bumps[0]);i++){
        contacts=bumps[i];frames(1,0,0);
        assert(manual_state.valid && fabsf(manual_state.angle-takeoff)<.05f);
        assert(fabsf(wheels[0]-left_at_takeoff)<.001f && fabsf(wheels[1]-right_at_takeoff)<.001f);
    }
    contacts=0;putf(body-0x350,20);putf(body-0x350+8,0);
    frames(60,0,0);assert(fabsf(manual_state.angle-takeoff)<.05f);
    frames(5,0,1);assert(manual_state.angle>takeoff+4);
    frames(20,0,-1);assert(manual_state.angle<takeoff-5);
    float landing=manual_state.angle;
    contacts=4;putf(body-0x350,0);putf(body-0x350+8,15);frames(1,0,0);
    assert(manual_state.valid && fabsf(manual_state.angle-landing)<560.f/60+.01f);
    puts("PASS two/one/zero-wheel contact, prolonged jump, mid-air input and bounded landing on both front wheels");
    contacts=4;frames(30,0,1);assert(manual_state.valid);
    sim[0x28]=1;frames(3,0,-1);assert(!manual_state.valid);sim[0x28]=0;
    config.enabled=0;frames(3,0,-1);assert(!manual_state.valid);
    config.enabled=1;assemble(body,NAN,0);frames(3,0,-1);assert(!manual_state.valid);
    puts("PASS pause, disabled assist and invalid steering data");

    putf(body-0x350,0);putf(body-0x350+8,0);
    verify_stops(92001,5,60,60);verify_stops(92001,6,74,74);verify_stops(92001,0,53.25f,21.5f);

    FH2Context render={0};render.rsi=(uintptr_t)car;
    visual_route(&render,NULL);assert(render.rflags&0x40);
    render.rflags=0;render.r14=2;visual_route(&render,NULL);assert(render.rflags==0);
    render.r14=1;manual_tick=GetTickCount64()-1000;expect_native();
    visual_route(&render,NULL);assert(render.rflags==0);
    frames(3,0,1);changed(NULL,"Enabled",0);expect_native();
    changed(NULL,"Enabled",1);frames(3,0,1);sim[0x28]=1;frames(3,0,1);expect_native();sim[0x28]=0;
    frames(3,0,1);slid(NULL,"Strength",0);expect_native();slid(NULL,"Strength",1.5f);
    puts("PASS previously unlisted tuning and untagged kits, visible front wheels, pause, zero strength, disabled and stale input");

    unsigned char *first=body,*first_car=car;
    unsigned char *second_alloc=malloc(0x2400);
    assert(second_alloc);unsigned char *second=second_alloc+0x400;
    memcpy(second_alloc,allocation,0x2400);
    memcpy(other,car,0x5000);putp(other+0x4c08,second);
    assemble(second,36,14);
    frames(30,0,-1);assert(manual_body==first);
    body=second;solver.rdi=(uintptr_t)body;expect_native();
    render.rsi=(uintptr_t)other;visual_route(&render,NULL);assert(!render.rflags);
    car=other;frames(30,0,1);assert(manual_body==second && manual_state.angle>35.9f);
    render.rsi=(uintptr_t)first_car;visual_route(&render,NULL);assert(!render.rflags);
    body=first;solver.rdi=(uintptr_t)body;expect_native();
    FH2Context destroy={0};destroy.rcx=(uintptr_t)second-0x3a0;
    unbind_wheels(&destroy,NULL);assert(!*wheel_binding((uintptr_t)second) && !manual_state.valid);
    car=first_car;frames(30,0,-1);assert(manual_body==first && manual_state.angle<-53);
    puts("PASS simultaneous player and AI, garage switch, reused physics and vehicle release");

    frames(60,0,0);
    slid(NULL,"SteeringSpeed",.5f);frames(1,0,1);float slow_angle=manual_state.angle;
    frames(60,0,0);slid(NULL,"SteeringSpeed",1.5f);frames(1,0,1);
    assert(manual_state.angle>slow_angle*2);
    frames(60,0,1);slid(NULL,"ReleaseSpeed",.5f);frames(1,0,0);float slow_return=manual_state.angle;
    frames(60,0,1);slid(NULL,"ReleaseSpeed",1.5f);frames(1,0,0);assert(manual_state.angle<slow_return-8);
    putf(body-0x350+8,20);slid(NULL,"FrontSlipDegrees",4);frames(60,0,1);
    assert(fabsf(manual_state.target-4)<.01f);
    slid(NULL,"FrontSlipDegrees",24);frames(60,0,1);assert(fabsf(manual_state.target-24)<.01f);
    assert(fmaxf(wheels[0],wheels[1])>.41f);putf(body-0x350+8,0);
    slid(NULL,"Deadzone",.15f);
    for(int side=-1;side<=1;side+=2){
        frames(60,side*.08f,side*.08f);
        assert(fabsf(wheels[0])<.001f && fabsf(wheels[1])<.001f);
        frames(60,0,side);
        assert(wheels[0]*side>.8f && wheels[1]*side>.8f);
    }
    slid(NULL,"Deadzone",0);
    frames(60,.08f,.08f);
    assert(wheels[0]>.06f && wheels[1]>.06f);
    config=assist_defaults();
    puts("PASS live steering tuning callbacks reach the physical front wheels");

    if(argc==2 && !strcmp(argv[1],"--database")){
        unsigned checked=0;int id;float limit,filtered;
        while(scanf("%d %f %f",&id,&limit,&filtered)==3){
            assert(id>0);verify_stops(id,0,limit,filtered);
            verify_stops(id,5,60,filtered);verify_stops(id,6,74,filtered);++checked;
        }
        assert(feof(stdin) && checked>0);
        printf("PASS stock, FD 60 and SLR 74 at both stops and moving countersteer for %u vehicles\n",checked);
    }
    return 0;
}
