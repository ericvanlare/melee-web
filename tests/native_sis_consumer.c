#include "gameplay_compat.h"
#include <sysdolphin/baselib/sislib.h>
#include <string.h>
#include <stdio.h>

/* Original SIS interpreter and style stack, including values whose byte order
 * matters. The font decoder test separately exercises immutable input/bounds. */
int melee_web_test_sis_consume(void* descriptor, unsigned count)
{
    HSD_Text text = {0};
    float width, height;
    u8 scaled[] = {14, 2, 0, 1, 0, 0x20, 0, 15, 0x20, 1, 0};
    u8 spaced[] = {10, 0xff, 0, 1, 0, 0x20, 0, 11, 0x20, 1, 0};
    char pointer_marker;
    HSD_SisLib_803A6048(64*1024);
    if(descriptor){
        HSD_SisLib_803A62A0(0,"SdSlChr.usd","SIS_SelCharData");
        if(HSD_SisLib_804D1124[0]!=descriptor)return 0;
    }
    text.x80.x = text.x80.y = 1;
    text.x6E = 256;
    text.string_buffer = HSD_SisLib_Alloc(text.x6E);
    memset(text.string_buffer,0,text.x6E);
    HSD_SisLib_803A8134(scaled,&text,&width,&height);
    if(width!=96 || height!=32 || text.x80.x!=1 || text.x80.y!=1){fprintf(stderr,"scaled width=%g height=%g scale=%g,%g\n",width,height,text.x80.x,text.x80.y);return 0;}
    HSD_SisLib_803A8134(spaced,&text,&width,&height);
    if(width!=63 || height!=32 || text.x78.x!=0){fprintf(stderr,"spaced width=%g height=%g spacing=%g\n",width,height,text.x78.x);return 0;}
    HSD_SisLib_803A7684(&text,(u8*)&pointer_marker,5);
    if((uintptr_t)HSD_SisLib_803A7F0C(&text,5)!=(uintptr_t)&pointer_marker){fprintf(stderr,"SIS pointer stack round trip failed\n");return 0;}
    /* Every original CSS string is interpreted up to its first line stop by
     * the real layout routine. Its original font/kerning bytes are in use. */
    text.kerning = 1;
    for(unsigned i=2;i<count;i++) {
        void* stream=((void**)descriptor)[i];
        if(stream)HSD_SisLib_803A8134(stream,&text,&width,&height);
    }
    HSD_SisLib_Free(text.string_buffer);
    HSD_SisLib_803A5FBC();
    return 1;
}
