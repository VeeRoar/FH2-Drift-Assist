/* Only the render-call output is changed; simulation matrices remain read-only. */
typedef struct VisualCar { uintptr_t spec, dynamics; int car, part; struct VisualCar *next; } VisualCar;
static VisualCar *visual_cars, *visual_specs;
static SRWLOCK visual_mutex=SRWLOCK_INIT;
static void visual_spec(uintptr_t spec,int car,int part){
    AcquireSRWLockExclusive(&visual_mutex);
    VisualCar **p=&visual_specs;
    while(*p && (*p)->spec!=spec)p=&(*p)->next;
    if(car==3886 || car==3890){
        if(!*p)*p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(**p));
        if(*p){(*p)->spec=spec;(*p)->car=car;(*p)->part=part;}
        /* This callback also describes upgrade previews. Never change an
           already assembled car from a temporary specification update. */
    }else if(*p){VisualCar *old=*p;*p=old->next;HeapFree(GetProcessHeap(),0,old);}
    ReleaseSRWLockExclusive(&visual_mutex);
}
static void visual_bind(uintptr_t spec,uintptr_t dynamics){
    AcquireSRWLockExclusive(&visual_mutex);
    int car=0,part=0;
    for(VisualCar *p=visual_specs;p;p=p->next)if(p->spec==spec){car=p->car;part=p->part;break;}
    /* The assembled spec, not the last UI row, owns the physical kit. The
       same exact marker is used by bind_wheels and survives spec copies. */
    float limits[2]={0};
    if(car && readmem((void*)(spec+0x5c),limits,sizeof(limits))){
        if(limits[0]==74.0f && limits[1]==nextafterf(74.0f,0.0f))part=car*1000+6;
        else if(limits[0]==60.0f && limits[1]==nextafterf(60.0f,0.0f))part=car*1000+5;
        else if(part-car*1000==5 || part-car*1000==6)part=car*1000;
    }
    VisualCar **link=&visual_cars;
    while(*link && (*link)->dynamics!=dynamics)link=&(*link)->next;
    if(car){
        if(!*link)*link=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(**link));
        if(*link){(*link)->spec=spec;(*link)->dynamics=dynamics;(*link)->car=car;(*link)->part=part;}
    }else if(*link){VisualCar *old=*link;*link=old->next;HeapFree(GetProcessHeap(),0,old);}
    static LONG bind_count;
    if(car || InterlockedIncrement(&bind_count)<=64){char msg[180];snprintf(msg,sizeof(msg),"Visual bind: spec=%p dynamics=%p car=%d part=%d",(void*)spec,(void*)dynamics,car,part);api->log(mod,msg);}
    ReleaseSRWLockExclusive(&visual_mutex);
}
static float dot3(const float *a,const float *b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
/* The animated path at f7f515 never calls f58240. Route only the selected
   front wheels through the game's existing physics-pose render path. This
   writes no animation flags or physics data, so kit switches are immediate. */
static void visual_route(FH2Context *ctx,void *unused){
    (void)unused;
    int wheel=(int)ctx->r14;
    if(wheel<0 || wheel>1)return;
    uintptr_t dynamics=0;
    if(!readmem((void*)(ctx->rsi+0x4c08),&dynamics,8))return;
    if(player_wheel_pose(dynamics)){
        ctx->rflags|=0x40; /* Draw this player's physical front-wheel pose. */
        return;
    }
    int car=0,part=0;
    AcquireSRWLockShared(&visual_mutex);
    for(VisualCar *p=visual_cars;p;p=p->next)if(p->dynamics==dynamics){car=p->car;part=p->part;break;}
    ReleaseSRWLockShared(&visual_mutex);
    int level=part-car*1000;
    if(!((car==3886 && (level==5 || level==6)) || (car==3890 && level>=0 && level<=4)))return;
    ctx->rflags|=0x40; /* ZF: take native JE f7f6c7 after the animation-availability test. */
    static volatile LONG count;
    LONG sample=InterlockedIncrement(&count);
    if(sample<=16 || sample%1200==0){char msg[200];snprintf(msg,sizeof(msg),"Visual route active: car=%d part=%d wheel=%d dynamics=%p",car,part,wheel,(void*)dynamics);api->log(mod,msg);}
}
static int visual_rotate(float *m,const float *basis,int drift){
    const float rad=0.01745329252f;
    for(int i=0;i<16;i++)if(!isfinite(m[i]))return 0;
    for(int i=0;i<12;i++)if(!isfinite(basis[i]))return 0;
    const float *up=basis+4;
    if(fabsf(dot3(up,up)-1)>0.02f)return 0;
    float x=dot3(m,basis),z=dot3(m,basis+8);
    if(x*x+z*z<0.2f)return 0;
    float current=atan2f(-z,x);
    /* Mirrored wheel axes may differ by pi; retain the same rest orientation. */
    if(current>1.570796327f)current-=3.141592654f;
    if(current<-1.570796327f)current+=3.141592654f;
    /* Native pose already includes the interpolated physical steering angle.
       Retain that exact pose for Charger drift, including caster/camber. */
    if(drift)return 0;
    float desired=fmaxf(-19*rad,fminf(19*rad,current*0.55f));
    if(!isfinite(desired)||fabsf(desired)>1.4f)return 0;
    float angle=desired-current,c=cosf(angle),s=sinf(angle);
    for(int row=0;row<3;row++){
        float *v=m+row*4,xv=v[0],yv=v[1],zv=v[2],d=dot3(up,v);
        v[0]=xv*c+(up[1]*zv-up[2]*yv)*s+up[0]*d*(1-c);
        v[1]=yv*c+(up[2]*xv-up[0]*zv)*s+up[1]*d*(1-c);
        v[2]=zv*c+(up[0]*yv-up[1]*xv)*s+up[2]*d*(1-c);
    }
    return 1;
}
static void visual_wheel(FH2Context *ctx,void *unused){
    (void)unused;
    int wheel=(int)ctx->rbp;
    if(wheel<0 || wheel>1)return;
    uintptr_t dynamics=0;
    if(!readmem((void*)(ctx->rsi+0x4c08),&dynamics,8))return;
    if(player_wheel_pose(dynamics))return;
    uintptr_t physics=dynamics; /* Live verified CCar physics getter at +0x4c08; +0x4be8 is matrix data. */
    int car=0,part=0;
    AcquireSRWLockShared(&visual_mutex);
    for(VisualCar *p=visual_cars;p;p=p->next)if(p->dynamics==(physics?physics:dynamics)){car=p->car;part=p->part;break;}
    ReleaseSRWLockShared(&visual_mutex);
    static LONG frame_count;
    if(InterlockedIncrement(&frame_count)<=8){char msg[180];snprintf(msg,sizeof(msg),"Visual frame: dynamics=%p car=%d part=%d wheel=%d",(void*)dynamics,car,part,wheel);api->log(mod,msg);}
    int level=part-car*1000,drift=(level==5 || level==6);
    if(!car || (!drift && (car!=3890 || level<0 || level>4)))return;
    if(car==3890 && drift)return;
    float m[16],basis[12],target;
    if(!readmem((void*)ctx->rax,m,sizeof(m)) ||
       !readmem((void*)(dynamics-0x380),basis,sizeof(basis)) ||
       !readmem((void*)((physics?physics:dynamics)+0x11c8+wheel*0x4b0),&target,4))return;
    if(car==3886 && drift){
        static volatile LONG logged;
        LONG sample=InterlockedIncrement(&logged);
        if(sample<=8 || sample%600==0){
            char msg[512];float limits[2]={0},other[2]={0};
            readmem((void*)(dynamics+8),limits,8);
            readmem((void*)((physics?physics:dynamics)+0x11c8+wheel*0x4b0),other,8);
            snprintf(msg,sizeof(msg),"Charger visual probe: wheel=%d part=%d demand=%.4f/%.4f wheelAngles=%.4f/%.4f axis=%.4f,%.4f,%.4f basisX=%.4f,%.4f,%.4f basisZ=%.4f,%.4f,%.4f",wheel,part,limits[0],limits[1],other[0],other[1],m[0],m[1],m[2],basis[0],basis[1],basis[2],basis[8],basis[9],basis[10]);api->log(mod,msg);
        }

    }
    if(visual_rotate(m,basis,drift)){
        SIZE_T n;WriteProcessMemory(GetCurrentProcess(),(void*)ctx->rax,m,sizeof(m),&n);
    }
}
