#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>
#include <string.h>
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
int melee_web_test_rumble_sequence(void)
{
    PadLibData previous = HSD_PadLibData;
    HSD_RumbleData saved[4];
    memcpy(saved,HSD_Rumble_804C22E0,sizeof(saved));
    HSD_PadRumbleListData pool[12] = {0};
    HSD_PadRumbleInit(12,pool);
    lb_80014574(0,123,0,0);
    HSD_RumbleData* port = &HSD_Rumble_804C22E0[0];
    int valid = port->nb_list == 1 && port->listdatap != NULL;
    if (valid) {
        /* Owned row zero is on for four frames, hard stop for one, then end.
         * This invokes the source interpreter without a physical actuator. */
        const unsigned expected[] = {2,2,2,2,0};
        for (unsigned i=0;i<5;i++) {
            u8 status=255;
            valid &= HSD_PadRumbleInterpret1(port->listdatap,&status)==0;
            valid &= status==expected[i];
        }
        u8 status=255;
        valid &= HSD_PadRumbleInterpret1(port->listdatap,&status)==1;
    }
    HSD_PadRumbleRemoveAll();
    valid &= port->nb_list==0;
    HSD_PadLibData=previous;
    memcpy(HSD_Rumble_804C22E0,saved,sizeof(saved));
    return valid;
}
