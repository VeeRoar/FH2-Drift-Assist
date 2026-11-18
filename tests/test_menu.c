/* Load the shipped DLL through its normal entry point. Run in an isolated
   Azimuth directory so changes and restarts exercise the real profile file. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/reference/fh2mod.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

typedef struct {const char *key,*en,*zh;} Text;
static const Text messages[]={
#define DRIFT_MENU_TEXT(id,en,zh) {#id,en,zh},
#include "../src/menu_text.def"
#undef DRIFT_MENU_TEXT
};
static FH2Setting rows[7];
static int count,zh,official,translated;
static const char *catalog,*description;
static void *game(void){return NULL;}
static void *code(uint32_t unused){(void)unused;return NULL;}
static FH2Mod *reg(const FH2ModInfo *info){
    assert(!strcmp(info->version,"0.5.1"));
    description=info->description;return (FH2Mod*)1;
}
static int language(FH2Mod *mod,const char *tag,const char *data,size_t size){
    assert(mod && !strcmp(tag,"zh-Hant") && strlen(data)==size);
    catalog=data;
    for(size_t i=0;i<sizeof(messages)/sizeof(*messages);i++){
        char entry[1200];
        snprintf(entry,sizeof(entry),"msgctxt \"%s\"\nmsgid \"%s\"\nmsgstr \"%s\"\n",
                 messages[i].key,messages[i].en,messages[i].zh);
        assert(strstr(data,entry));
        assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,messages[i].zh,-1,NULL,0)>1);
    }
    return (int)(sizeof(messages)/sizeof(*messages));
}
static const char *translate(FH2Mod *mod,const char *key,const char *english){
    assert(mod && catalog);
    for(size_t i=0;i<sizeof(messages)/sizeof(*messages);i++){
        if(!strcmp(key,messages[i].key)){
            assert(!strcmp(english,messages[i].en));
            translated++;
            return zh?messages[i].zh:english;
        }
    }
    assert(0);return NULL;
}
static void describe(FH2Mod *mod,const char *text){assert(mod);description=text;}
static int add(FH2Mod *mod,const FH2Setting *s){
    assert(mod && count<(int)(sizeof(rows)/sizeof(*rows)) && s->label && s->description);
    for(int i=0;i<count;i++)assert(strcmp(rows[i].key,s->key));
    rows[count++]=*s;return 1;
}
static void sql(FH2Mod *mod,const char *s){(void)mod;(void)s;}
static void logline(FH2Mod *mod,const char *s){(void)mod;(void)s;}
static FH2Setting *row(const char *key){
    for(int i=0;i<count;i++)if(!strcmp(key,rows[i].key))return &rows[i];
    assert(0);return NULL;
}
static void check_text(const char *value,const char *key){
    for(size_t i=0;i<sizeof(messages)/sizeof(*messages);i++){
        if(!strcmp(key,messages[i].key)){
            assert(!strcmp(value,zh?messages[i].zh:messages[i].en));return;
        }
    }
    assert(0);
}
int main(int argc,char **argv){
    assert(argc==5);
    zh=!strcmp(argv[2],"zh");official=!strcmp(argv[3],"10");
    wchar_t dir[MAX_PATH];GetCurrentDirectoryW(MAX_PATH,dir);
    FH2ModApi api={0};api.size=sizeof(api);api.api_version=official?10:8;
    api.azimuth_dir=dir;api.register_mod=reg;api.game_base=game;api.wait_for_code=code;
    api.add_setting=add;api.add_sql=sql;api.log=logline;
    if(official){api.add_translation=language;api.tr=translate;api.set_mod_description=describe;}
    HMODULE dll=LoadLibraryA(argv[1]);assert(dll);
    int (*init)(const FH2ModApi*)=(void*)GetProcAddress(dll,"fh2mod_init");
    assert(init && !init(&api) && count==7);
    assert(translated==(official?15:0));check_text(description,"Description");
    for(int i=0;i<count;i++){
        const char *key=!strcmp(rows[i].key,"FrontSlipDegrees")?"FrontSlip":rows[i].key;
        char label[80],help[80];snprintf(label,sizeof(label),"%sLabel",key);snprintf(help,sizeof(help),"%sHelp",key);
        check_text(rows[i].label,label);check_text(rows[i].description,help);
    }
    const char *keys[]={"Strength","SteeringSpeed","ReleaseSpeed","FrontSlipDegrees","Deadzone"};
    const float defaults[]={1.5f,1,1,12,.04f};
    const float modified[]={1.15f,.65f,1.45f,17,.09f};
    FH2Setting *deadzone=row("Deadzone");
    assert(deadzone->min==0 && deadzone->max==.15f && deadzone->step==.01f && deadzone->display_scale==100);
    int custom=!strcmp(argv[4],"read");
    for(size_t i=0;i<sizeof(keys)/sizeof(*keys);i++){
        FH2Setting *s=row(keys[i]);
        assert(s->kind==FH2_SETTING_SLIDER && s->step>0 && s->on_slide);
        assert(fabsf(s->slider_value-(custom?modified[i]:defaults[i]))<.0001f);
        if(!strcmp(argv[4],"write"))s->on_slide(s->user,s->key,modified[i]);
    }
    assert(row("Enabled")->value==!custom && row("ChaseCamera")->value==!custom);
    if(!strcmp(argv[4],"write")){
        row("Enabled")->on_change(NULL,"Enabled",0);
        row("ChaseCamera")->on_change(NULL,"ChaseCamera",0);
    }
    printf("PASS API %s %s %s: 7 localized settings, 15 texts and saved values\n",argv[3],argv[2],argv[4]);
    return 0;
}
