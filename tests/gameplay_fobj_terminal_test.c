#include <sysdolphin/baselib/fobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned callbacks;
static float output;
void HSD_Panic(char* file, u32 line, char* reason)
{ fprintf(stderr,"%s:%u: %s\n",file,line,reason);exit(2); }
void __assert(char* file,u32 line,char* reason){HSD_Panic(file,line,reason);}
static void update(void* object,enum_t type,HSD_ObjData* value)
{ (void)object;(void)type;output=value->fv;++callbacks; }
static void check(int ok,const char* reason){if(!ok){fprintf(stderr,"%s\n",reason);exit(1);}}
int main(int argc,char** argv)
{
    unsigned char stream[]={1,0,0};
    HSD_FObj f={0};f.ad_head=stream;f.length=sizeof(stream);
    f.startframe=-40;f.obj_type=12;f.frac_value=f.frac_slope=0x88;
    if(argc>1&&!strcmp(argv[1],"--invalid")){
        f.op=HSD_A_OP_LIN;FObjUpdateAnim(&f,NULL,update);
        check(0,"Unknown interpolation must still reject");
    }
    /* Original FD's delayed zero visibility constant, then an authored one.
     * Drive the original parser rather than manufacturing its terminal state. */
    for(unsigned value=0;value<2;++value){
        stream[1]=value?128:0;f.frac_value=f.frac_slope=0x87;
        callbacks=0;output=-100;HSD_FObjReqAnimAll(&f,0);
        for(unsigned frame=0;frame<39;++frame)HSD_FObjInterpretAnim(&f,NULL,update,1);
        check(callbacks==0,"Delayed terminal constant fired early");
        HSD_FObjInterpretAnim(&f,NULL,update,1);
        check(callbacks==1&&output==(float)value,"Terminal constant lost its authored value");
        check(f.op_intrp==HSD_A_OP_NONE,"Compatibility callback altered parser state");
    }
    puts("Original FObj terminal constants passed");return 0;
}
