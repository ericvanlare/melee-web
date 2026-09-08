/* GX calls are capture boundaries; both original ftMaterial readers and the
 * original HSD_TExpSetReg execute. This is an operand/lifetime test, not pixels. */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static GXColor captured[8];static unsigned writes,stages;
static HSD_TevDesc stage;
struct ftCommonData* p_ftCommonData;
void ftMaterial_800BF260(void){abort();}
void __assert(char* f,u32 line,char* message){fprintf(stderr,"%s:%u: %s\n",f,line,message);abort();}
void OSReport(const char* format,...){va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);}
s32 lbGetFreeColorRegister(s32 first,HSD_MObj* m,HSD_TExp* t){(void)m;(void)t;return first;}
s32 lb_8000CC8C(s32 r){(void)r;return GX_CC_KONST;}
s32 lb_8000CCA4(s32 r){(void)r;return GX_TEV_KCSEL_K0;}
s32 lb_8000CD90(s32 r){(void)r;return GX_CA_KONST;}
s32 lb_8000CDA8(s32 r){(void)r;return GX_TEV_KASEL_K0_A;}
int HSD_StateAssignTev(void){return GX_TEVSTAGE0;}
void HSD_SetupTevStage(HSD_TevDesc* p){stage=*p;stages++;}
void HSD_StateInvalidate(int mask){if(mask!=0x10)abort();}
void GXPixModeSync(void){}
void GXSetTevKColor(GXTevKColorID id,GXColor color){captured[id]=color;writes++;}
void GXSetTevColor(GXTevRegID id,GXColor color){captured[4+id-GX_TEVREG0]=color;writes++;}
void ftCo_8009F75C(Fighter* fp,melee_source_bool alpha){(void)fp;(void)alpha;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Material ABI check failed: %s\n",#x);return 1;}}while(0)
int main(void)
{
    Fighter fp={0};HSD_MObj material={0};HSD_TExp expression={0};
    unsigned char saved_class[sizeof(ftMObj)];memcpy(saved_class,&ftMObj,sizeof(ftMObj));
    CHECK(sizeof(ftMObj)==sizeof(HSD_MObjInfo));
    fp.x61D=255;fp.x488.x7C_flag2=1;fp.x488.x7C_light_enable=1;
    fp.x488.x50_light_color=(GXColor){3,17,93,255};
    CHECK(ftMaterial_800BF534(&fp,&material,&expression,0)==&expression);
    CHECK(expression.cnst.type==HSD_TE_CNST&&expression.cnst.comp==HSD_TE_RGB&&expression.cnst.ctype==HSD_TE_U8);
    CHECK(captured[0].r==3&&captured[0].g==17&&captured[0].b==93&&stages==1);
    CHECK(stage.flags==TEVCONF_MODE&&stage.u.tevconf.alpha_d==GX_CA_APREV);
    memset(&fp.x488,0,sizeof(fp.x488));fp.x488.x7C_color_enable=1;
    fp.x488.x2C_hex=(GXColor){17,34,51,68};writes=stages=0;
    ftMaterial_800BF6BC(&fp,&material,NULL);
    CHECK(writes==2&&stages==1);
    CHECK(captured[0].r==17&&captured[0].g==34&&captured[0].b==51);
    CHECK(captured[4].r==68&&captured[4].g==68&&captured[4].b==68);
    CHECK(stage.flags==TEVCONF_MODE&&stage.u.tevconf.alpha_d==GX_CA_APREV);
    fp.x61D=128;writes=stages=0;
    ftMaterial_800BF6BC(&fp,&material,NULL);
    CHECK(writes==2&&stages==1);
    CHECK(captured[4].r==68&&captured[4].g==68&&captured[4].b==68&&captured[4].a==128);
    CHECK(stage.u.tevconf.alpha_d==GX_CA_KONST);
    CHECK(memcmp(saved_class,&ftMObj,sizeof(ftMObj))==0);
    puts("Original fighter material templates and named RGB stack lifetime: passed");return 0;
}
