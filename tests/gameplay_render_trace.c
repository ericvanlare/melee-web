#include "gameplay_render.h"
#include "gameplay_bootstrap.h"
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <melee/cm/camera.h>
#include <assert.h>
#include <stdio.h>
int main(void){
    char error[256];
    MeleeWebRenderSettings s={640,480,{0,20,80},{0,10,0},40,1,1000,1ULL<<5};
    for(unsigned pass=0;pass<2;pass++){
        assert(melee_web_gameplay_startup(4U*1024U*1024U,error,sizeof(error)));
        s.near_plane=0;assert(!melee_web_render_begin(&s,error,sizeof(error)));s.near_plane=1;
        MeleeWebRender* render=melee_web_render_begin(&s,error,sizeof(error));
        if(!render){fprintf(stderr,"%s\n",error);return 1;}
        assert(Camera_80030A50()&&Camera_80030A50()->gxlink_prios==(1ULL<<5));
        HSD_CObj* camera=Camera_80030A50()->hsd_obj;
        assert(camera->viewport.xmax==640&&camera->scissor.bottom==480);
        assert(!melee_web_render_begin(&s,error,sizeof(error)));
        s.width=960;s.height=540;assert(melee_web_render_update(render,&s,error,sizeof(error)));
        assert(camera->projection_param.perspective.aspect==960.0f/540.0f);
        assert(melee_web_render_end(render,error,sizeof(error)));
        assert(!Camera_80030A50()&&!HSD_CObjGetCurrent());
        assert(!melee_web_gameplay_stats().objects);
        assert(melee_web_gameplay_shutdown(error,sizeof(error)));
        s.width=640;s.height=480;
    }
    puts("Original camera descriptor allocation/settings/teardown/restart passed; drawing requires Aurora");return 0;
}
