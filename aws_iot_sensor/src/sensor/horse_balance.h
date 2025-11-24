#ifndef HORSE_BALANCE_H_
#define HORSE_BALANCE_H_

#include <stdbool.h>
#include <stdint.h>

/* 三种状态 */
typedef enum {
    HB_STATE_BALANCED = 0,        /* ✅ 平衡 */
    HB_STATE_LR_IMBALANCE,        /* ⚠️ 左右不平衡 */
    HB_STATE_FH_IMBALANCE         /* ⚠️ 前后不平衡 */
} hb_state_t;

typedef struct {
    bool baseline_set;
    float heading0, roll0, pitch0;   /* 初始姿态 */

    float lr_thresh_deg;             /* 左右阈值（度） */
    float fh_thresh_deg;             /* 前后阈值（度） */

    hb_state_t state;                /* 当前状态 */
    uint32_t   last_change_ts;       /* 最近一次状态变化的时间 */

    float last_roll;                 /* 记录变化时的姿态，方便以后用 */
    float last_pitch;
} horse_balance_t;

/* 初始化：传入左右/前后各自的角度阈值（度） */
void horse_balance_init(horse_balance_t *hb,
                        float lr_thresh_deg,
                        float fh_thresh_deg);

/* 清空 baseline，下次 update 会重新记录初始姿态 */
void horse_balance_clear_baseline(horse_balance_t *hb);

/* 
 * 每次读到新的 Heading/Roll/Pitch 调一次。
 * 参数 changed_if_nonnull：如果状态发生变化则置 true。
 * 返回值：当前状态（平衡/左右/前后）
 */
hb_state_t horse_balance_update(horse_balance_t *hb,
                                float heading,
                                float roll,
                                float pitch,
                                bool *changed_if_nonnull);

/* 一个简单的 “是否 warning” 封装：只要不是 BALANCED 就算 warning */
static inline bool horse_balance_is_warning(const horse_balance_t *hb)
{
    return hb->state != HB_STATE_BALANCED;
}

#endif /* HORSE_BALANCE_H_ */