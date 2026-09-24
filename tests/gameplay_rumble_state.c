#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
int melee_web_test_rumble_sequence(void)
{
    /* Use the world's published pool, as a CSS confirm does. A test-owned
     * pool would hide missing world initialization. */
    if (HSD_PadLibData.rumble_info.max_list != 12) return 0;
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
    return valid;
}

int melee_web_test_rumble_queue(void)
{
    lb_80014574(0,124,0,0);
    return HSD_Rumble_804C22E0[0].nb_list == 1;
}

int melee_web_test_rumble_clear(void)
{
    for (unsigned i=0;i<4;i++)
        if (HSD_Rumble_804C22E0[i].nb_list ||
            HSD_Rumble_804C22E0[i].listdatap) return 0;
    return 1;
}
