/* Execute the entire original translation unit, retaining only the reset path.
 * Costume storage is authored input; the source registry and reset code are real. */
#include <melee/ft/ftdata.c>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "reset check failed: %s at %d\n", #x, __LINE__); exit(1); } } while (0)
#define COSTUMES(name) UnkCostumeStruct name[sizeof(name) / sizeof(name[0])]
COSTUMES(ftMr_CostumeList);
COSTUMES(ftFx_CostumeList);
COSTUMES(ftCa_CostumeList);
COSTUMES(ftDk_CostumeList);
COSTUMES(ftKb_CostumeList);
COSTUMES(ftKp_CostumeList);
COSTUMES(ftLk_CostumeList);
COSTUMES(ftSk_CostumeList);
COSTUMES(ftNs_CostumeList);
COSTUMES(ftPe_CostumeList);
COSTUMES(ftPp_CostumeList);
COSTUMES(ftNn_CostumeList);
COSTUMES(ftPk_CostumeList);
COSTUMES(ftSs_CostumeList);
COSTUMES(ftYs_CostumeList);
COSTUMES(ftPr_CostumeList);
COSTUMES(ftMt_CostumeList);
COSTUMES(ftLg_CostumeList);
COSTUMES(ftMs_CostumeList);
COSTUMES(ftZd_CostumeList);
COSTUMES(ftCl_CostumeList);
COSTUMES(ftDr_CostumeList);
COSTUMES(ftFc_CostumeList);
COSTUMES(ftPc_CostumeList);
COSTUMES(ftGw_CostumeList);
COSTUMES(ftGn_CostumeList);
COSTUMES(ftFe_CostumeList);
COSTUMES(ftMh_CostumeList);
COSTUMES(ftCh_CostumeList);
COSTUMES(ftBo_CostumeList);
COSTUMES(ftGl_CostumeList);
COSTUMES(ftGk_CostumeList);
COSTUMES(ftSb_CostumeList);
int main(void)
{
    int action_counts[FTKIND_MAX], pair_counts[FTKIND_MAX];
    for (int kind = 0; kind < FTKIND_MAX; ++kind) {
        action_counts[kind] = ftData_Table_Unk0[kind].count;
        pair_counts[kind] = ftData_UnkIntPairs[kind].count;
    }
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        for (int kind = 0; kind < FTKIND_MAX; ++kind) {
            gFtDataList[kind] = (void*) 0x1000;
            ftData_Table_Unk0[kind].data = (void*) 0x2000;
            ftData_UnkIntPairs[kind].data = (void*) 0x3000;
            ft_8045996C[kind] = 0x45450000 + kind;
            for (int c = 0; c < CostumeListsForeachCharacter[kind].numCostumes; ++c) {
                UnkCostumeStruct* s = &CostumeListsForeachCharacter[kind].costume_list[c];
                s->joint = (void*) 0x4000; s->pad_x8 = 0x12345678;
                s->x4 = (void*) 0x5000; s->pad_xC = 0xabcdef01;
                s->pad_x10 = 0xfedcba98; s->x14_archive = (void*) 0x6000;
            }
        }
        for (int i = 0; i < 6; ++i) {
            ft_8045993C[i].pad_x0 = 0x12345678;
            ft_8045993C[i].pad_x4[0] = 0x37; ft_8045993C[i].pad_x4[1] = 0x59;
            ft_8045993C[i].x6_b0 = 1; ft_8045993C[i].x6_b1_b2 = 3;
        }
        ft_800852B0();
        for (int kind = 0; kind < FTKIND_MAX; ++kind) {
            CHECK(gFtDataList[kind] == NULL);
            CHECK(ftData_Table_Unk0[kind].data == NULL);
            CHECK(ftData_UnkIntPairs[kind].data == NULL);
            CHECK(ftData_Table_Unk0[kind].count == action_counts[kind]);
            CHECK(ftData_UnkIntPairs[kind].count == pair_counts[kind]);
            CHECK(ft_8045996C[kind] == 0x45450000 + kind);
            for (int c = 0; c < CostumeListsForeachCharacter[kind].numCostumes; ++c) {
                UnkCostumeStruct* s = &CostumeListsForeachCharacter[kind].costume_list[c];
                CHECK(s->joint == NULL && s->pad_x8 == 0);
                CHECK(s->x4 == (void*) 0x5000 && s->pad_xC == 0xabcdef01);
                CHECK(s->pad_x10 == 0xfedcba98 && s->x14_archive == (void*) 0x6000);
            }
        }
        for (int i = 0; i < 6; ++i) {
            CHECK(ft_8045993C[i].pad_x0 == 0);
            CHECK(ft_8045993C[i].x6_b0 == 0 && ft_8045993C[i].x6_b1_b2 == 0);
            CHECK(ft_8045993C[i].pad_x4[0] == 0x37 && ft_8045993C[i].pad_x4[1] == 0x59);
        }
    }
    puts("Original fighter registry reset and preserved fields: passed");
    return 0;
}
