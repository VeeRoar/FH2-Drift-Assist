/* FH2 1.0.0.64, data-to-dynamics assembly and end of per-wheel steering. */
static const unsigned char init_bytes[]={0x4d,0x8d,0xa7,0x90,0,0,0,0xc5,0xfa,0x58,0x92,0x1c,5,0,0,0xc5};
static const unsigned char angle_bytes[]={0x48,0x8b,0x5c,0x24,0x60,0x48,0x8b,0x6c,0x24,0x68,0x48,0x8b,0x74,0x24,0x70,0xc5};
/* One float step below the physical stop marks the fitted drift suspension. */
#define SLR_FILTERED_LOCK 0x1.27fffep+6f
typedef struct WheelBinding {
    uintptr_t steering;
    float limit;
    int angle_kit;
    struct WheelBinding *next;
    volatile LONG geometry_mm; /* low 16 bits wheelbase, high 16 bits track */
} WheelBinding;
/* Buckets hold live physics instances. Collisions have no effect on car eligibility. */
static WheelBinding *wheel_bindings[64];
static volatile LONG lock_ready, observed_directions;

static WheelBinding **wheel_binding(uintptr_t steering){
    WheelBinding **link=&wheel_bindings[((steering>>6)^(steering>>16))%64];
    while(*link && (*link)->steering!=steering)link=&(*link)->next;
    return link;
}
/* Equipped angle kits use the game's physical wheel pose even when the
   input assist is switched off. Stock traffic keeps its own animation. */
static int player_wheel_pose(uintptr_t body){
    AcquireSRWLockShared(&manual_lock);
    WheelBinding *node=*wheel_binding(body);
    int active=(node && node->angle_kit) || fresh_rack((void*)body);
    ReleaseSRWLockShared(&manual_lock);
    return active;
}
static void forget_wheel_binding(WheelBinding **link){
    WheelBinding *old=*link;
    if(old){*link=old->next;HeapFree(GetProcessHeap(),0,old);}
}
static void invalidate_wheel_command(uintptr_t steering){
    if(InterlockedCompareExchangePointer(&trace_player,NULL,(void*)steering)==(void*)steering){
        InterlockedExchangePointer(&trace_confirmed,NULL);
        manual_body=NULL;
        manual_state=(DirectRack){0};
        InterlockedExchange(&reset_player_motion,1);
    }
}
static void unbind_wheels(FH2Context *ctx,void *unused){
    (void)unused;
    uintptr_t steering=ctx->rcx+0x3a0;
    AcquireSRWLockExclusive(&manual_lock);
    invalidate_wheel_command(steering);
    forget_wheel_binding(wheel_binding(steering));
    ReleaseSRWLockExclusive(&manual_lock);
}
static void bind_wheels(FH2Context *ctx,void *unused){
    (void)unused;
    /* CCarDynamics calls the angle solver on its +0x3a0 interface. */
    uintptr_t steering=ctx->rsi+0x3a0;
    visual_bind(ctx->rdi,steering);
    float limits[2];
    int valid=readmem((void*)(ctx->rdi+0x5c),limits,sizeof(limits)) &&
              isfinite(limits[0]) && limits[0]>0 && limits[0]<=90;
    AcquireSRWLockExclusive(&manual_lock);
    invalidate_wheel_command(steering);
    WheelBinding **link=wheel_binding(steering);
    if(valid){
        if(!*link)*link=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(**link));
        if(*link){
            (*link)->steering=steering;
            (*link)->limit=limits[0];
            (*link)->geometry_mm=0;
            (*link)->angle_kit=(limits[0]==74.0f && limits[1]==SLR_FILTERED_LOCK) ||
                                (limits[0]==60.0f && limits[1]==nextafterf(60.0f,0.0f));
        }
    }else{
        forget_wheel_binding(link);
    }
    ReleaseSRWLockExclusive(&manual_lock);
}

static void set_wheel_angles(FH2Context *ctx,void *unused){
    (void)unused;
    unsigned char *steering=(unsigned char*)ctx->rdi;
    AcquireSRWLockShared(&manual_lock);
    WheelBinding *node=*wheel_binding(ctx->rdi);
    int player=InterlockedCompareExchangePointer(&trace_player,NULL,NULL)==steering;
    if(!node || (!player && !node->angle_kit)){ReleaseSRWLockShared(&manual_lock);return;}
    float limit=node->limit;
    float wheelbase=ctx->xmm[8].f[0];
    unsigned char *wheels[]={steering+0x11c8,steering+0x1678};
    if(!*(int*)(wheels[0]+0x39c)||!*(int*)(wheels[1]+0x39c)){ReleaseSRWLockShared(&manual_lock);return;}
    float track=*(float*)(wheels[1]+0x468)-*(float*)(wheels[0]+0x468);
    if(!isfinite(wheelbase) || fabsf(wheelbase)<.001f || !isfinite(track)){
        ReleaseSRWLockShared(&manual_lock);return;
    }
    unsigned wb_mm=(unsigned)lroundf(fminf(fabsf(wheelbase)*1000,65535.f));
    unsigned track_mm=(unsigned)lroundf(fminf(fabsf(track)*1000,65535.f));
    InterlockedExchange(&node->geometry_mm,(LONG)((track_mm<<16)|wb_mm));
    if(player)InterlockedExchangePointer(&trace_confirmed,steering);
    const float rad=0.01745329252f;
    DirectRack adjustment={0};
    if(fresh_rack(steering))adjustment=manual_state;
    if(!adjustment.valid && !node->angle_kit){ReleaseSRWLockShared(&manual_lock);return;}
    for(int channel=0;channel<2;channel++){
        float request=adjustment.valid?adjustment.angle*rad:((float*)(steering+8))[channel];
        if(!isfinite(request))continue;
        float angle=fminf(fabsf(request),limit*rad);
        /* Quarter Ackermann for a drift angle kit. The requested angle sets
           the inside wheel directly. The outside wheel follows a wider arc. */
        int inner=(signbit(request)!=0) ^ ((track/wheelbase)>0);
        float outer=atan2f(sinf(angle),cosf(angle)+0.25f*fabsf(track/wheelbase)*sinf(angle));
        ((float*)wheels[inner])[channel]=copysignf(angle,request);
        ((float*)wheels[1-inner])[channel]=copysignf(outer,request);
        if(channel==0 && angle>73.5f*rad){
            LONG bit=request<0?1:2;
            if(!(InterlockedOr(&observed_directions,bit)&bit)){
                char msg[160];snprintf(msg,sizeof(msg),"Drift wheel lock running: demand %.2f, front wheels %.2f / %.2f degrees.",
                    request/rad,((float*)wheels[0])[0]/rad,((float*)wheels[1])[0]/rad);
                api->log(mod,msg);
            }
        }
    }
    ReleaseSRWLockShared(&manual_lock);
}
