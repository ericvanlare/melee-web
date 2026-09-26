#include "gameplay_ps_math.h"
#include "gameplay_fres.h"
#include <sysdolphin/baselib/cobj.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent lane transcription of GALE01r2 PSMTXInverse, 80342320..80342414.
 * This is a test oracle only; the runtime uses explicit scalar cofactors. */
typedef struct { float x, y; } Pair;
static Pair mul(Pair a, Pair b) { return (Pair){a.x*b.x,a.y*b.y}; }
static Pair madd(Pair a, Pair b, Pair c) { return (Pair){fmaf(a.x,b.x,c.x),fmaf(a.y,b.y,c.y)}; }
static Pair msub(Pair a, Pair b, Pair c) { return (Pair){fmaf(a.x,b.x,-c.x),fmaf(a.y,b.y,-c.y)}; }
static Pair scale(Pair a, float s) { return mul(a,(Pair){s,s}); }
static unsigned lane_reference(const Mtx m, Mtx out)
{
    Pair f0={m[0][0],1},f1={m[0][1],m[0][2]};
    Pair f2={m[1][0],1},f3={m[1][1],m[1][2]};
    Pair f4={m[2][0],1},f5={m[2][1],m[2][2]};
    Pair f6={f1.y,f0.x},f7={f3.y,f2.x};
    Pair f11=mul(f3,f6),f13=mul(f5,f7),f8={f5.y,f4.x};
    f11=msub(f1,f7,f11);
    Pair f12=mul(f1,f8);
    f13=msub(f3,f8,f13);
    Pair f10=mul(f3,f4);
    f12=msub(f5,f6,f12);
    Pair f9=mul(f0,f5);
    f8=mul(f1,f2);
    f10=msub(f2,f5,f10);
    f7=mul(f0,f13);
    f9=msub(f1,f4,f9);
    f7=madd(f2,f12,f7);
    f8=msub(f0,f3,f8);
    f7=madd(f4,f11,f7);
    if(f7.x==0)return 0;
    float estimate=melee_web_fres(f7.x);
    float twice=estimate+estimate,square=estimate*estimate;
    float reciprocal=-fmaf(f7.x,square,-twice);
    f13=scale(f13,reciprocal);f12=scale(f12,reciprocal);
    f11=scale(f11,reciprocal);f10=scale(f10,reciprocal);
    f9=scale(f9,reciprocal);f8=scale(f8,reciprocal);
    f6=scale(f13,m[0][3]);
    f6=madd(f12,(Pair){m[1][3],m[1][3]},f6);
    f6=madd(f11,(Pair){m[2][3],m[2][3]},f6);
    float last=f10.x*m[0][3];
    last=fmaf(f9.x,m[1][3],last);
    last=-fmaf(f8.x,m[2][3],last);
    Mtx result={{f13.x,f12.x,f11.x,-f6.x},
                {f13.y,f12.y,f11.y,-f6.y},
                {f10.x,f9.x,f8.x,last}};
    memcpy(out,result,sizeof(result));return 1;
}

static Mtx allocated;
static unsigned allocations;
void* HSD_MtxAlloc(void) { ++allocations; return &allocated; }

static unsigned fallback_differences;
static unsigned fallback_numeric_differences;
static void check(const Mtx input)
{
    Mtx expected,actual,alias,fallback;
    memset(expected,0xa5,sizeof(expected));memset(actual,0xa5,sizeof(actual));
    unsigned ok=lane_reference(input,expected);
    assert(melee_web_ps_mtx_inverse(input,actual)==ok);
    assert(memcmp(expected,actual,sizeof(actual))==0);
    memcpy(alias,input,sizeof(alias));
    Mtx original;memcpy(original,alias,sizeof(original));
    assert(PSMTXInverse(alias,alias)==ok);
    assert(memcmp(alias,ok?expected:original,sizeof(alias))==0);
    memset(actual,0xa5,sizeof(actual));
    assert(MTXInverse(input,actual)==ok);
    assert(memcmp(actual,expected,sizeof(actual))==0);
    if(ok) {
        assert(C_MTXInverse(input,fallback));
        fallback_differences+=memcmp(fallback,expected,sizeof(fallback))!=0;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
            fallback_numeric_differences+=fallback[row][col]!=expected[row][col];
        /* Execute the original lazy camera consumer and its cached path. */
        HSD_CObj camera={0};
        memcpy(camera.view_mtx,input,sizeof(input[0])*3);
        camera.flags=0x80000000u;
        unsigned before=allocations;
        assert(HSD_CObjGetInvViewingMtxPtrDirect(&camera)==allocated);
        assert(allocations==before+1 && !(camera.flags&0x80000000u));
        assert(memcmp(allocated,expected,sizeof(allocated))==0);
        camera.view_mtx[0][0]+=1; /* A clean cache must not recompute. */
        assert(HSD_CObjGetInvViewingMtxPtrDirect(&camera)==allocated);
        assert(allocations==before+1);
        assert(memcmp(allocated,expected,sizeof(allocated))==0);
    }
}

int main(void)
{
    Mtx identity={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    Mtx singular={{1,2,3,4},{1,2,3,4},{0,0,0,0}};
    check(identity);check(singular);
    for(unsigned n=1;n<=128;++n) {
        Mtx matrix;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c) {
            int value=(int)((n*(r+3)*17+c*29)%113)-56;
            matrix[r][c]=(float)value/17.0f;
        }
        matrix[0][0]+=4.0f;matrix[1][1]+=5.0f;matrix[2][2]+=6.0f;
        check(matrix);
    }
    assert(fallback_differences>0);
    assert(fallback_numeric_differences>0);
    printf("inverse cases=130 aliases=260 camera=%u fallback_differences=%u\n",
           allocations,fallback_differences);
}
