#include "hsd_native_arrays.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    unsigned char data[48]={0};char error[128];uint32_t size=0;
    MeleeWebPObjAttribute a={9,2,1,4,0,12,data,24};
    assert(!melee_web_native_array_bound(9,data,12,&size));
    MeleeWebNativeArrays* first=melee_web_native_arrays_register(&a,1,error,sizeof(error));assert(first);
    assert(melee_web_native_array_bound(9,data,12,&size)&&size==24);
    assert(!melee_web_native_array_bound(10,data,12,&size));
    assert(!melee_web_native_array_bound(9,data+1,12,&size));
    assert(!melee_web_native_array_bound(9,data,6,&size));
    a.byte_size=48;MeleeWebNativeArrays* second=melee_web_native_arrays_register(&a,1,error,sizeof(error));assert(second);
    assert(melee_web_native_array_bound(9,data,12,&size)&&size==48);
    melee_web_native_arrays_remove(second);
    assert(melee_web_native_array_bound(9,data,12,&size)&&size==24);
    melee_web_native_arrays_remove(first);assert(!melee_web_native_array_bound(9,data,12,&size));
    a.byte_size=0;assert(!melee_web_native_arrays_register(&a,1,error,sizeof(error)));
    a.byte_size=48;a.stride=256;assert(!melee_web_native_arrays_register(&a,1,error,sizeof(error)));
    puts("Native indexed-array exact identity/bounds/lifetime checks passed");return 0;
}
