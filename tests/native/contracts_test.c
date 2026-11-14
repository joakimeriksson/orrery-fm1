/* SPDX-License-Identifier: GPL-3.0-only */
#include "core/instrument.h"
#include "core/effect.h"
#include "core/project_format.h"
#include "test.h"
#include <stddef.h>
int main(void)
{
    /* Independent expected wire offsets prevent an internally consistent but
     * accidentally changed macro budget from passing unnoticed. */
    const uint32_t offsets[]={MD_PROJECT_TRACK_OFFSET,MD_PROJECT_PATTERN_OFFSET,MD_PROJECT_STEP_OFFSET,
        MD_PROJECT_SECTION_OFFSET,MD_PROJECT_LOCK_OFFSET,MD_PROJECT_SOUND_OFFSET,MD_PROJECT_EFFECT_OFFSET,MD_PROJECT_BYTES};
    const uint32_t expected[]={128,256,384,6528,7040,7328,7840,8096};
    for (uint32_t i=0;i<sizeof offsets/sizeof offsets[0];i++) CHECK(offsets[i]==expected[i]);
    CHECK(offsetof(md_step_t,probability)==5 && sizeof(md_step_t)==6);
    CHECK(MD_MAX_LOCKS==48 && 11u*12u+124u==MD_PROJECT_EFFECT_BYTES);
    CHECK((MD_TARGET_REVERB | 4095u)==0x5fffu);
    CHECK((MD_TARGET_INSERT_2 & MD_TARGET_GROUP_MASK)==0x2000u);
    puts("PASS portable headers and independent v1 wire budget");
    return 0;
}
