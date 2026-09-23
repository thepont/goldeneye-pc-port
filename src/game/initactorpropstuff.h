#ifndef _INITACTORPROPSTUFF_H_
#define _INITACTORPROPSTUFF_H_

#include <ultra64.h>

struct ModelAnimation;
struct weapon_firing_animation_table;
struct anim_group_info;
struct StruckAnim;

void reset_counter_rand_body_head(void);
u32 sub_GAME_7F0001F0(void *ani, int aniid, int param_3);
s32 sub_GAME_7F000290(struct ModelAnimation *anim,
                      s32 startframe, s32 endframe);
s32 initResolveAnimGroupTable(struct weapon_firing_animation_table *animconfig);
void initResolveAnimGroups(struct anim_group_info **groups);
s32 initResolveAnimTable(struct StruckAnim *entries);
void initWeaponAnimGroups(void);
void casingsInit(void);
void initCasingPool(void);

#endif
