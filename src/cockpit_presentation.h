static int local_cockpit_angle(unsigned char *car,float *result,float *left,float *right){
    if(!InterlockedCompareExchange(&cockpit_enabled,0,0))return 0;
    int applied=0;
    AcquireSRWLockShared(&manual_lock);
    if(car && car==cockpit_player_car && fresh_rack(manual_body) &&
       ptr(car+0x4c08)==manual_body){
        WheelBinding *binding=*wheel_binding((uintptr_t)manual_body);
        if(binding){
            CockpitWheels *s=&binding->cockpit;
            AcquireSRWLockShared(&s->lock);
            if(s->valid && GetTickCount64()-s->tick<=80){
                *left=s->left;
                *right=s->right;
                applied=cockpit_angle(*left,*right,binding->limit,result);
            }
            ReleaseSRWLockShared(&s->lock);
        }
    }
    ReleaseSRWLockShared(&manual_lock);
    return applied;
}

static void cockpit_trace(const char *path,volatile LONG *counter,float left,float right,
                          float native,float result){
    LONG n=InterlockedIncrement(counter);
    if(n==1 || n%120==0){
        char line[176];
        snprintf(line,sizeof(line),"CockpitSync %s frame=%ld wheelsDeg=%.2f/%.2f nativeDeg=%.2f sentDeg=%.2f",
                 path,n,left*57.29578f,right*57.29578f,native*57.29578f,result*57.29578f);
        api->log(mod,line);
    }
}

/* r14 owns the deferred presentation command. xmm1 is a steering wheel
   angle in radians. All other parameters, including driver motion, stay native. */
static void present_cockpit(FH2Context *ctx,void *unused){
    (void)unused;
    float angle,left,right;
    if(!local_cockpit_angle((unsigned char*)ctx->r14,&angle,&left,&right))return;
    float native=ctx->xmm[1].f[0];
    ctx->xmm[1].f[0]=angle;
    static volatile LONG frames;
    cockpit_trace("presentation",&frames,left,right,native,angle);
}

/* The local driver's full pose uses the animation graph instead of the
   cached presentation pose. At f7c74b FH2 converts xmm0 from [-1,1] to
   [0,1] and sets GameScalars:Turn on this car's driver graph. */
static void present_driver_turn(FH2Context *ctx,void *unused){
    (void)unused;
    float angle,left,right;
    if(!local_cockpit_angle((unsigned char*)ctx->rbx,&angle,&left,&right))return;
    float native=ctx->xmm[0].f[0]*cockpit_half_turn;
    ctx->xmm[0].f[0]=angle/cockpit_half_turn;
    static volatile LONG frames;
    cockpit_trace("driver-graph",&frames,left,right,native,angle);
}

static void install_cockpit_sync(void){
    static const unsigned char native_call[]={
        0xc4,0xc1,0x7a,0x10,0x86,0x38,0x4f,0,0,  /* car + 4f38 */
        0xc4,0xc1,0x7a,0x58,0x8e,0x30,0x4f,0,0,  /* + filtered angle */
        0xc4,0xc1,0x7a,0x10,0x9e,0x58,0x51,0,0,  /* fourth argument */
        0x4c,0x8d,0x45,0x20,0xff,0x90,0xc8,0x01,0,0
    };
    static const unsigned char graph_call[]={
        0x48,0x8b,0x03,0x48,0x8b,0xcb,0xff,0x90,0x60,0x01,0,0,
        0xc4,0xc1,0x7a,0x58,0xc9,0xc5,0xf2,0x59,0xd6,0x45,0x33,0xc9,
        0x48,0x8d,0x15,0xa2,0x08,0x08,0x01,0x49,0x8b,0xce,
        0xe8,0xda,0xf7,0x77,0
    };
    unsigned char seen[sizeof(native_call)],graph[sizeof(graph_call)];
    if(!lock_ready || !readmem(base+0x2a7db8,seen,sizeof(seen)) ||
       memcmp(seen,native_call,sizeof(seen)) ||
       !readmem(base+0xf7c73f,graph,sizeof(graph)) || memcmp(graph,graph_call,sizeof(graph)) ||
       memcmp(base+0x1ffd000,"GameScalars:Turn",sizeof("GameScalars:Turn"))){
        api->log(mod,"Cockpit sync unavailable: wheel solver or animation signatures did not match.");
        return;
    }
    FH2Hook *hook=api->hook_instruction(mod,"Local cockpit steering angle",
                                        base+0x2a7dca,present_cockpit,NULL);
    FH2Hook *driver=api->hook_instruction(mod,"Driver animation Turn scalar",
                                          base+0xf7c74b,present_driver_turn,NULL);
    if(!hook || !driver || !api->enable_hooks(mod,driver)){
        api->log(mod,"Cockpit sync unavailable: driver animation hook could not be enabled.");
        return;
    }
    if(!api->enable_hooks(mod,hook)){
        api->disable_hooks(mod,driver);
        api->log(mod,"Cockpit sync unavailable: presentation hook could not be enabled.");
        return;
    }
    api->log(mod,"Cockpit steering sync ready: driver Turn graph and steering presentation.");
}
