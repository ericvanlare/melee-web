#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <melee/gm/gmresultplayer.static.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjgxlink.h>

static HSD_GObj camera_gobjs[3];
static unsigned camera_count;
static HSD_CObj camera_cobj;
static HSD_CObjDesc* camera_desc;
static Vec3 camera_eye;
static Vec3 camera_interest;
static Scissor camera_scissor;
u8 HSD_GObj_CameraKind;

/* The camera fixture exercises source setup, including the second winner
 * camera. Retained rendering callbacks must fail if invoked: this is not a
 * renderer test. */
static void unexpected_results_callback(void) __attribute__((noreturn));
static void unexpected_results_callback(void)
{
    abort();
}

melee_source_bool HSD_CObjSetCurrent(HSD_CObj* cobj)
{
    (void) cobj;
    /* Skip the renderer-only EFB branch while retaining the source camera
     * setup and return-value path under test. */
    return 0;
}

u32 gm_80160854(u8 slot, u8 team, u8 is_teams, u8 slot_type)
{
    (void) slot;
    (void) team;
    (void) is_teams;
    (void) slot_type;
    unexpected_results_callback();
}

GXColor gm_80160968(u32 value)
{
    (void) value;
    unexpected_results_callback();
}

void HSD_SetEraseColor(u8 r, u8 g, u8 b, u8 a)
{
    (void) r;
    (void) g;
    (void) b;
    (void) a;
    unexpected_results_callback();
}

void HSD_CObjEraseScreen(HSD_CObj* cobj, s32 enable_color, s32 enable_alpha,
                         s32 enable_depth)
{
    (void) cobj;
    (void) enable_color;
    (void) enable_alpha;
    (void) enable_depth;
    unexpected_results_callback();
}

void Camera_800313E0(HSD_GObj* gobj, u64 priorities)
{
    (void) gobj;
    (void) priorities;
    unexpected_results_callback();
}

void HSD_ImageDescCopyFromEFB(HSD_ImageDesc* image, u16 x, u16 y,
                              GXBool clear, melee_source_bool sync)
{
    (void) image;
    (void) x;
    (void) y;
    (void) clear;
    (void) sync;
    unexpected_results_callback();
}

void HSD_CObjEndCurrent(void)
{
    unexpected_results_callback();
}

HSD_GObj* Player_GetEntity(s32 slot)
{
    (void) slot;
    unexpected_results_callback();
}

s32 ftLib_800876B4(HSD_GObj* gobj)
{
    (void) gobj;
    unexpected_results_callback();
}

HSD_GObj* GObj_Create(u16 classifier, u8 p_link, u8 priority)
{
    (void) classifier;
    (void) p_link;
    (void) priority;
    if(camera_count==3)unexpected_results_callback();
    return &camera_gobjs[camera_count++];
}

HSD_CObj* HSD_CObjLoadDesc(HSD_CObjDesc* desc)
{
    camera_desc = desc;
    return &camera_cobj;
}

void HSD_GObjObject_80390A70(HSD_GObj* gobj, u8 kind, void* obj)
{
    (void) gobj;
    (void) kind;
    (void) obj;
}

void HSD_GObj_80390ED0(HSD_GObj* gobj, u32 mask)
{
    (void) gobj;
    (void) mask;
    unexpected_results_callback();
}

float Player_800360D8(s32 slot)
{
    (void) slot;
    return 0.0F;
}

void HSD_CObjSetEyePosition(HSD_CObj* cobj, Vec3* value)
{
    (void) cobj;
    camera_eye = *value;
}

void HSD_CObjSetInterest(HSD_CObj* cobj, Vec3* value)
{
    (void) cobj;
    camera_interest = *value;
}

void HSD_CObjSetScissor(HSD_CObj* cobj, Scissor* value)
{
    (void) cobj;
    camera_scissor = *value;
}

void GObj_SetupGXLinkMax(HSD_GObj* gobj, GObj_RenderFunc render_cb,
                         u32 priority)
{
    (void) gobj;
    (void) render_cb;
    (void) priority;
}

extern HSD_GObj* fn_8017A318(s32 arg0);

static int fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

int main(void)
{
    static const uint16_t dimensions[6][4] = {
        { 0x0050, 0x0050, 0x0046, 0x0034 },
        { 0x006E, 0x0072, 0x0064, 0x004A },
        { 0x0034, 0x0034, 0x0034, 0x0034 },
        { 0x004A, 0x004A, 0x004A, 0x004A },
        { 0x000C, 0x0008, 0x0006, 0x0000 },
        { 0x000E, 0x000E, 0x0006, 0x0000 },
    };
    static const int16_t score[4][4] = {
        { 0, 0, 0, 0 },
        { -14, 14, 0, 0 },
        { -18, 0, 18, 0 },
        { -22, -7, 7, 22 },
    };
    static const int16_t x22f4[4][4] = {
        { 24, 0, 0, 0 },
        { 21, 21, 0, 0 },
        { 18, 18, 18, 0 },
        { 14, 14, 14, 14 },
    };
    ResultsDisplayLayout* display = &melee_web_results_display;
    const uint16_t* actual_dimensions[6] = {
        display->state.dim_w1, display->state.dim_h1,
        display->state.dim_w2, display->state.dim_h2,
        display->state.scissor_y, display->state.scissor_x,
    };
    HSD_GObj* gobj_marker = (HSD_GObj*) (uintptr_t) 0x1234;
    HSD_JObj* jobj_marker = (HSD_JObj*) (uintptr_t) 0x5678;
    HSD_GObj* result_gobj;
    unsigned i;
    unsigned j;

    memset(display, 0, sizeof(*display));
    melee_web_results_init_layout_tables();

    for (i = 0; i < 6; ++i) {
        for (j = 0; j < 4; ++j) {
            if (actual_dimensions[i][j] != dimensions[i][j]) {
                return fail("Results dimensions/scissor halfword order changed");
            }
        }
    }
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
            if (display->state.score_tbl[i].h[j] != score[i][j] ||
                display->state.x22F4[i].h[j] != x22f4[i][j]) {
                return fail("Results score halfword order changed");
            }
        }
    }

    /* The old retail linker overlay is now one typed aggregate. Exercise each
     * live alias so a future layout change cannot silently split the state. */
    display->gobjs[0] = gobj_marker;
    display->jobjs[0] = jobj_marker;
    if (lbl_8046E38C[0] != gobj_marker || lbl_8046E39C[0] != jobj_marker) {
        return fail("Results object aliases do not share live aggregate storage");
    }
    lbl_8046E3AC.score_tbl[0].h[0] = -123;
    if (display->state.score_tbl[0].h[0] != -123) {
        return fail("Results state alias does not share live aggregate storage");
    }
    for (i = 0; i < 4; ++i) {
        display->state.match_end.player_standings[i].is_big_loser = 1;
        display->state.match_end.player_standings[i].slot_type = 0;
    }
    display->state.match_end.is_teams = 0;
    display->state.variant[0] = 0;
    display->state.char_kind[0] = 0;
    result_gobj = fn_8017A318(0);
    if (result_gobj != &camera_gobjs[0] ||
        camera_desc != (HSD_CObjDesc*) &gmResultCameraDesc) {
        return fail("Results camera setup did not use the source descriptor");
    }
    if (camera_scissor.left != 270 || camera_scissor.right != 370 ||
        camera_scissor.top != 124 || camera_scissor.bottom != 276 ||
        fabsf(camera_eye.x - 0.2F) > 0.001F ||
        fabsf(camera_eye.y - 107.3F) > 0.001F ||
        fabsf(camera_eye.z + 198.0F) > 0.001F ||
        fabsf(camera_interest.x - 0.2F) > 0.001F ||
        fabsf(camera_interest.y - 107.3F) > 0.001F) {
        return fail("Results camera setup did not consume authored fields");
    }
    display->state.match_end.player_standings[0].is_big_loser = 0;
    result_gobj = fn_8017A318(0);
    if (camera_count != 3 || result_gobj != &camera_gobjs[2]) {
        return fail("Results winner camera did not return its source GObj");
    }
    /* These are authored camera inputs consumed by fn_8017A318 after the
     * retail color-array overlay is removed. Keep the check on source-owned
     * values so a copied or synthetic camera table cannot pass silently. */
    if (gmResultCharacterScaleData[0].x0[0] != 0.2F ||
        gmResultCharacterScaleData[0].x0[4] != 10.0F ||
        gmResultCharacterScaleData[0].x20[0] != 3.6F ||
        gmResultCharacterData.slot_off[0][1][3] != -4.0F) {
        return fail("Results camera scale/slot fields are not source-owned");
    }
    if (gmResultCameraDesc.projection_type != PROJ_PERSPECTIVE ||
        gmResultCameraDesc.viewport.xmax != 640 ||
        gmResultCameraDesc.viewport.ymax != 480 ||
        gmResultCameraDesc.ffar != 5000.0F) {
        return fail("Results camera descriptor fields changed");
    }
    printf("Results typed aggregate aliases and authored halfword tables: passed\n");
    return 0;
}
