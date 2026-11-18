typedef struct {const char *key,*english,*traditional;} MenuText;
#define DRIFT_MENU_TEXT(id,en,zh) static const MenuText id={#id,en,zh};
#include "menu_text.def"
#undef DRIFT_MENU_TEXT

/* Keep gettext's source text and the fallback labels in sync. */
static const char traditional_catalog[] =
    "msgid \"\"\nmsgstr \"\"\n\"Language: zh-Hant\\n\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\n"
#define DRIFT_MENU_TEXT(id,en,zh) "msgctxt \"" #id "\"\nmsgid \"" en "\"\nmsgstr \"" zh "\"\n\n"
#include "menu_text.def"
#undef DRIFT_MENU_TEXT
    ;
static int native_translation;
static const char *menu_text(const MenuText *text){
    if(native_translation)return api->tr(mod,text->key,text->english);
    return chinese?text->traditional:text->english;
}

typedef struct {
    const char *key;
    const wchar_t *ini_key;
    const MenuText *label,*help;
    float *value,low,high,step,display_scale;
    int decimals,storage_scale;
} DriftSlider;
static const DriftSlider sliders[]={
    {"Strength",L"Strength",&StrengthLabel,&StrengthHelp,&config.strength,0,1.5f,.05f,100,0,100},
    {"SteeringSpeed",L"SteeringSpeed",&SteeringSpeedLabel,&SteeringSpeedHelp,&config.steering_speed,.5f,1.5f,.05f,100,0,100},
    {"ReleaseSpeed",L"ReleaseSpeed",&ReleaseSpeedLabel,&ReleaseSpeedHelp,&config.release_speed,.5f,1.5f,.05f,100,0,100},
    {"Deadzone",L"Deadzone",&DeadzoneLabel,&DeadzoneHelp,&config.deadzone,0,.15f,.01f,100,0,100},
    {"FrontSlipDegrees",L"FrontSlipDegrees",&FrontSlipLabel,&FrontSlipHelp,&config.front_slip_deg,4,24,1,1,0,1},
};
#define SLIDER_COUNT (sizeof(sliders)/sizeof(sliders[0]))
static volatile LONG pending_save;
static void save_int(const wchar_t *key,int value){
    if(!ini[0])return;
    wchar_t text[24];swprintf(text,24,L"%d",value);
    if(!WritePrivateProfileStringW(L"DriftAssist",key,text,ini))
        InterlockedExchange(&pending_save,1);
}
static int load_int(const wchar_t *key,int fallback,int low,int high){
    wchar_t text[32],*end;
    if(!GetPrivateProfileStringW(L"DriftAssist",key,L"",text,32,ini)){
        save_int(key,fallback);
        return fallback;
    }
    long value=wcstol(text,&end,10);
    if(end==text || *end)return fallback;
    return value<low?low:value>high?high:(int)value;
}
static void retry_save(void){
    if(!InterlockedExchange(&pending_save,0))return;
    AcquireSRWLockExclusive(&lock);
    save_int(L"Enabled",config.enabled);
    save_int(L"ChaseCamera",camera_enabled);
    for(size_t i=0;i<SLIDER_COUNT;i++)
        save_int(sliders[i].ini_key,(int)lroundf(*sliders[i].value*sliders[i].storage_scale));
    ReleaseSRWLockExclusive(&lock);
}
static void changed(void *user,const char *key,int value){
    (void)user;
    AcquireSRWLockExclusive(&lock);
    if(!strcmp(key,"Enabled")){
        config.enabled=!!value;
        if(!value)clear_manual();
        save_int(L"Enabled",config.enabled);
    }else if(!strcmp(key,"ChaseCamera")){
        camera_enabled=!!value;save_int(L"ChaseCamera",camera_enabled);
    }
    ReleaseSRWLockExclusive(&lock);
}
static void slid(void *user,const char *key,float value){
    (void)user;
    if(!isfinite(value))return;
    for(size_t i=0;i<SLIDER_COUNT;i++){
        const DriftSlider *s=&sliders[i];
        if(strcmp(s->key,key))continue;
        AcquireSRWLockExclusive(&lock);
        float bounded=rack_clip(value,s->low,s->high);
        int stored=(int)lroundf(bounded*s->storage_scale);
        *s->value=(float)stored/s->storage_scale;
        if(s->value==&config.strength && !stored)clear_manual();
        save_int(s->ini_key,stored);
        ReleaseSRWLockExclusive(&lock);
        return;
    }
}
static void add_toggle(const char *key,const MenuText *label,const MenuText *help,int value){
    FH2Setting row={0};
    row.size=sizeof(row);row.key=key;row.label=menu_text(label);row.description=menu_text(help);
    row.value=value;row.on_change=changed;
    api->add_setting(mod,&row);
}
static void register_settings(void){
    config=assist_defaults();
    config.enabled=load_int(L"Enabled",config.enabled,0,1);
    camera_enabled=load_int(L"ChaseCamera",1,0,1);
    add_toggle("Enabled",&EnabledLabel,&EnabledHelp,config.enabled);
    for(size_t i=0;i<SLIDER_COUNT;i++){
        const DriftSlider *s=&sliders[i];
        int scale=s->storage_scale;
        *s->value=(float)load_int(s->ini_key,(int)lroundf(*s->value*scale),
                                (int)lroundf(s->low*scale),(int)lroundf(s->high*scale))/scale;
        FH2Setting row={0};
        row.size=sizeof(row);row.kind=FH2_SETTING_SLIDER;row.key=s->key;
        row.label=menu_text(s->label);row.description=menu_text(s->help);
        row.slider_value=*s->value;row.min=s->low;row.max=s->high;row.step=s->step;
        row.display_scale=s->display_scale;row.decimals=s->decimals;row.on_slide=slid;
        api->add_setting(mod,&row);
    }
    add_toggle("ChaseCamera",&ChaseCameraLabel,&ChaseCameraHelp,camera_enabled);
}
