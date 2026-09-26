#include "gameplay_effect_banks.h"
#include "gameplay_bootstrap.h"
#include <sysdolphin/baselib/particle.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(void*)==4 && sizeof(int)==4,"Original particle bank pointer-word ABI");
_Static_assert(offsetof(HSD_PSCmdList,cmdList)==0x3c && offsetof(HSD_PSTexGroup,texTable)==0x18,
               "Original particle descriptor layouts");
extern u32* hsd_804D0948[65];
extern HSD_Particle* hsd_804D0908[16];
extern HSD_PSFormGroup** psFormGroupArray[65];
struct MeleeWebEffectBank {
    int* commands;
    int* textures;
    MeleeWebEffectBankStats stats;
    u32* saved_ref;
    HSD_PSTexGroup** saved_textures;
    HSD_PSFormGroup** saved_forms;
    HSD_PSCmdList** saved_commands;
    int saved_texture_count,saved_command_count;
    unsigned attached;
};
static MeleeWebEffectBank* published[65];
int melee_web_effect_bank_is_published(uint32_t bank)
{
    return bank<65&&published[bank]&&published[bank]->attached&&
        published[bank]->stats.bank==bank;
}
int melee_web_effect_bank_has_command(uint32_t bank,uint32_t command)
{
    if(bank>=65||!published[bank])return 0;
    const MeleeWebEffectBank* h=published[bank];
    return command>=h->stats.first_command&&
        command-h->stats.first_command<h->stats.command_count&&
        ptclref_804D0E5C[bank]&&ptclref_804D0E5C[bank][command];
}
MeleeWebEffectBank* melee_web_effect_bank_alias(const MeleeWebNativeDat* d,
    const MeleeWebEffectBank* source,uint32_t bank)
{
    if(!source||bank>=65||bank==source->stats.bank)
        d->reject(d->context,"Invalid particle bank alias");
    MeleeWebEffectBank* h=d->allocate(d->context,1,sizeof(*h));
    h->commands=source->commands;h->textures=source->textures;
    h->stats=source->stats;h->stats.bank=bank;h->stats.particle_bank_ready=0;
    return h;
}
static int fail(char* e,size_t n,const char* why){if(e&&n)snprintf(e,n,"%s",why);return 0;}
static int success(char* e,size_t n){if(e&&n)e[0]=0;return 1;}
#define REQUIRE(c,m) do{if(!(c))d->reject(d->context,m);}while(0)
#define WORD(o) d->word(d->context,(o))
#define HALF(o) d->half(d->context,(o))
#define NEW(n,t) ((t*)d->allocate(d->context,(n),sizeof(t)))
static uint32_t relative(const MeleeWebNativeDat* d,uint32_t base,uint32_t length,uint32_t slot,size_t bytes)
{
    const uint32_t offset=WORD(slot);
    REQUIRE(offset && !(offset&3) && offset<=length && bytes<=length-offset && base<=UINT32_MAX-offset,
            "Particle bank relative pointer is null, unaligned or outside its bank");
    d->region(d->context,base+offset,bytes);return base+offset;
}
static size_t tiled_bytes(const MeleeWebNativeDat* d,uint32_t format,uint32_t width,uint32_t height)
{
    REQUIRE(width&&height&&width<=1024&&height<=1024,"Particle texture dimensions exceed bounds");
    uint32_t tw=4,th=4,bytes=32;
    switch(format){
    case 0:case 8:case 14:tw=8;th=8;break;
    case 1:case 2:case 9:tw=8;th=4;break;
    case 3:case 4:case 5:break;
    case 6:bytes=64;break;
    default:REQUIRE(0,"Unsupported particle texture format");
    }
    return (size_t)((width+tw-1)/tw)*((height+th-1)/th)*bytes;
}
MeleeWebEffectBank* melee_web_effect_bank_decode(const MeleeWebNativeDat* d,uint32_t table,
    uint32_t command_bytes,uint32_t texture_bytes,uint32_t bank)
{
    REQUIRE(bank<65,"Particle bank index exceeds original capacity");
    const uint32_t cb=d->pointer(d->context,table,8),tb=d->pointer(d->context,table+4,4);
    return melee_web_effect_bank_decode_roots(d,cb,tb,command_bytes,texture_bytes,bank);
}
MeleeWebEffectBank* melee_web_effect_bank_decode_roots(const MeleeWebNativeDat* d,uint32_t cb,uint32_t tb,
    uint32_t command_bytes,uint32_t texture_bytes,uint32_t bank)
{
    REQUIRE(bank<65,"Particle bank index exceeds original capacity");
    REQUIRE(cb!=UINT32_MAX&&tb!=UINT32_MAX,"Particle banks are absent");
    REQUIRE(command_bytes>=8&&texture_bytes>=4&&command_bytes<=8U*1024U*1024U&&texture_bytes<=16U*1024U*1024U,
            "Particle bank byte bounds are invalid");
    d->region(d->context,cb,command_bytes);d->region(d->context,tb,texture_bytes);
    const uint16_t version=HALF(cb),subversion=HALF(cb+2);
    REQUIRE(version==0||(version>=0x40&&version<=0x43),"Unsupported original particle bank version");
    const uint32_t first=version?WORD(cb+4):0,count=WORD(cb+(version?8:4)),header=version?12:8;
    const uint32_t groups=WORD(tb);
    REQUIRE(first<=65535&&count&&count<=2048&&first+count<=65536&&groups&&groups<=256,
            "Particle command/texture count exceeds bounds");
    REQUIRE(header+(size_t)count*4<=command_bytes&&4+(size_t)groups*4<=texture_bytes,"Particle pointer table is truncated");
    MeleeWebEffectBank* h=NEW(1,MeleeWebEffectBank);
    h->stats=(MeleeWebEffectBankStats){bank,first,count,groups,0,0,0,0};
    // Original version40+ subtracts first from cmdBank+3. Allocate genuine
    // prefix storage so that biased pointer remains inside its native object.
    const uint32_t prefix=version&&first>3?first-3:0;
    int* buffer=NEW(prefix+header/4+count,int);h->commands=buffer+prefix;
    ((u16*)h->commands)[0]=version;((u16*)h->commands)[1]=subversion;
    h->commands[1]=version?(int)first:(int)count;if(version)h->commands[2]=(int)count;
    h->textures=NEW(groups+1,int);h->textures[0]=(int)groups;
    uint32_t* offsets=NEW(count,uint32_t);
    for(uint32_t i=0;i<count;++i){
        offsets[i]=relative(d,cb,command_bytes,cb+header+4*i,0x3d);
        REQUIRE(offsets[i]>=cb+header+4*count,"Particle command overlaps its pointer table");
    }
    for(uint32_t i=0;i<count;++i){
        uint32_t end=cb+command_bytes;
        for(uint32_t j=0;j<count;++j)if(offsets[j]>offsets[i]&&offsets[j]<end)end=offsets[j];
        const uint32_t at=offsets[i],length=end-at;
        REQUIRE(length>=0x3d&&length<=65536,"Particle command stream region exceeds bounds");
        HSD_PSCmdList* cmd=d->allocate(d->context,1,length);
        cmd->type=HALF(at);cmd->texGroup=HALF(at+2);cmd->genLife=HALF(at+4);cmd->life=HALF(at+6);
        REQUIRE(cmd->texGroup<groups,"Particle command texture group is out of range");
        cmd->kind=(WORD(at+8)&0xf1ffffffU)|0x08000000U; // Exact original Locate transformation.
        for(uint32_t field=0xc;field<0x3c;field+=4){
            const uint32_t bits=WORD(at+field);float value;memcpy(&value,&bits,4);
            REQUIRE(isfinite(value),"Particle command header contains a nonfinite scalar");
            memcpy((char*)cmd+field,&value,4);
        }
        memcpy(cmd->cmdList,d->region(d->context,at+0x3c,length-0x3c),length-0x3c);
        ((HSD_PSCmdList**)(h->commands+header/4))[i]=cmd;
    }
    for(uint32_t i=0;i<groups;++i){
        const uint32_t at=relative(d,tb,texture_bytes,tb+4+4*i,0x18);
        const uint32_t images=WORD(at),format=WORD(at+4),tlutfmt=WORD(at+8),width=WORD(at+12),height=WORD(at+16);
        const uint16_t palnum=HALF(at+20),palflag=HALF(at+22);
        REQUIRE(images&&images<=256&&!(palflag&~1u)&&palnum<=256,"Particle texture group counts/flags exceed bounds");
        const int indexed=format==8||format==9;
        const uint32_t palettes=indexed?((palflag&1)?1:palnum?palnum:images):0;
        /* psdisp narrows HSD_PSTexGroup::tlutfmt to u8 before GXInitTlutObj.
         * Preserve the full authored word below, but validate the format that
         * the original consumer actually passes to GX. */
        REQUIRE(!indexed||(uint8_t)tlutfmt<=2,"Particle palette format is unsupported");
        const size_t image_size=tiled_bytes(d,format,width,height);
        d->region(d->context,at,0x18+(size_t)(images+palettes)*4);
        REQUIRE((size_t)(at-tb)+0x18+(size_t)(images+palettes)*4<=texture_bytes,
                "Particle texture pointer table crosses its bank");
        HSD_PSTexGroup* group=d->allocate(d->context,1,0x18+(size_t)(images+palettes)*sizeof(void*));
        group->num=images;group->fmt=format;group->tlutfmt=tlutfmt;group->width=width;group->height=height;
        group->palnum=palnum;group->palflag=palflag;
        for(uint32_t t=0;t<images+palettes;++t){
            const size_t bytes=t<images?image_size:(format==8?32:512);
            const uint32_t source=relative(d,tb,texture_bytes,at+0x18+4*t,bytes);
            REQUIRE(!(source&31),"Particle tiled payload is not32-byte aligned");
            group->texTable[t]=d->allocate(d->context,1,bytes);
            memcpy(group->texTable[t],d->region(d->context,source,bytes),bytes);
        }
        ((HSD_PSTexGroup**)(h->textures+1))[i]=group;
        h->stats.images+=images;h->stats.palettes+=palettes;
    }
    return h;
}
int melee_web_effect_bank_attach(MeleeWebEffectBank* h,char* e,size_t n)
{
    if(!h||h->attached||published[h->stats.bank])return fail(e,n,"Particle bank publication is missing or already owned");
    if(!melee_web_gameplay_stats().generation)return fail(e,n,"Particle registration requires an owned source world");
    if(hsd_804D78E0)return fail(e,n,"Remove original generators before replacing their banks");
    for(unsigned i=0;i<16;++i)if(hsd_804D0908[i])
        return fail(e,n,"Remove original particles before replacing their banks");
    const uint32_t b=h->stats.bank;
    h->saved_ref=hsd_804D0948[b];h->saved_textures=psTexGroupArray[b];h->saved_forms=psNumCmdList[b];
    h->saved_commands=ptclref_804D0E5C[b];h->saved_command_count=psCmdListArray[b];
    memcpy(&h->saved_texture_count,&psFormGroupArray[b],4);
    psInitDataBankLoad(b,h->commands,h->textures,NULL,NULL);
    published[b]=h;h->attached=1;h->stats.particle_bank_ready=1;return success(e,n);
}
static int bank_matches(const MeleeWebEffectBank* h)
{
    const uint32_t b=h->stats.bank;
    HSD_PSCmdList** commands=((u16*)h->commands)[0]?
        (HSD_PSCmdList**)(h->commands+3)-h->stats.first_command:
        (HSD_PSCmdList**)(h->commands+2);
    int texture_count;memcpy(&texture_count,&psFormGroupArray[b],4);
    return published[b]==h&&!hsd_804D0948[b]&&!psNumCmdList[b]&&
        texture_count==(int)h->stats.texture_groups&&
        psTexGroupArray[b]==(HSD_PSTexGroup**)(h->textures+1)&&
        ptclref_804D0E5C[b]==commands&&
        psCmdListArray[b]==(int)(h->stats.first_command+h->stats.command_count);
}
int melee_web_effect_bank_load_owned(uint32_t b,const void* commands,
    const void* textures,char* e,size_t n)
{
    if(!melee_web_effect_bank_is_published(b))
        return fail(e,n,"Source particle load has no typed owner");
    MeleeWebEffectBank* h=published[b];
    if(commands!=h->commands||textures!=h->textures)
        return fail(e,n,"Source particle load does not match its typed roots");
    /* Accept the existing publication or the complete reset performed by
     * hsd_80398A08. A partial or foreign replacement is still an owner error. */
    if(!bank_matches(h)&&(hsd_804D0948[b]||psNumCmdList[b]||
        psFormGroupArray[b]||psTexGroupArray[b]||ptclref_804D0E5C[b]||
        psCmdListArray[b]))
        return fail(e,n,"Source particle load found another bank publication");
    psInitDataBankLoad(b,h->commands,h->textures,NULL,NULL);
    return success(e,n);
}
int melee_web_effect_bank_detach(MeleeWebEffectBank* h,char* e,size_t n)
{
    if(!h)return fail(e,n,"Particle bank owner is absent");
    if(!h->attached)return success(e,n);
    const uint32_t b=h->stats.bank;
    if(!bank_matches(h))
        return fail(e,n,"Particle bank publication was replaced by another owner");
    if(hsd_804D78E0)return fail(e,n,"Remove original generators before releasing their banks");
    for(unsigned i=0;i<16;++i)if(hsd_804D0908[i])
        return fail(e,n,"Remove original particles before releasing their banks");
    hsd_804D0948[b]=h->saved_ref;psTexGroupArray[b]=h->saved_textures;psNumCmdList[b]=h->saved_forms;
    ptclref_804D0E5C[b]=h->saved_commands;psCmdListArray[b]=h->saved_command_count;
    memcpy(&psFormGroupArray[b],&h->saved_texture_count,4);
    published[b]=NULL;h->attached=0;h->stats.particle_bank_ready=0;return success(e,n);
}
int melee_web_effect_bank_stats(const MeleeWebEffectBank* h,MeleeWebEffectBankStats* out,char* e,size_t n)
{
    if(!h||!out)return fail(e,n,"Particle bank stats input is absent");
    *out=h->stats;return success(e,n);
}

void* melee_web_effect_bank_commands(MeleeWebEffectBank* h){return h?h->commands:NULL;}
void* melee_web_effect_bank_textures(MeleeWebEffectBank* h){return h?h->textures:NULL;}
