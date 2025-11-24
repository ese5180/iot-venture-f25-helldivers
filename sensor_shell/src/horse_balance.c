#include "horse_balance.h"
#include <zephyr/kernel.h>
#include <math.h>

static float absf(float x) { return x < 0.0f ? -x : x; }

void horse_balance_init(horse_balance_t *hb,
                        float lr_thresh_deg,
                        float fh_thresh_deg)
{
    hb->baseline_set  = false;
    hb->heading0 = hb->roll0 = hb->pitch0 = 0.0f;

    hb->lr_thresh_deg = lr_thresh_deg;
    hb->fh_thresh_deg = fh_thresh_deg;

    hb->state = HB_STATE_BALANCED;
    hb->last_change_ts = 0;
    hb->last_roll = hb->last_pitch = 0.0f;
}

void horse_balance_clear_baseline(horse_balance_t *hb)
{
    hb->baseline_set = false;
}

/* 内部：根据 Δroll / Δpitch 判定状态 */
static hb_state_t decide_state(const horse_balance_t *hb,
                               float d_roll, float d_pitch)
{
    float a_roll  = absf(d_roll);
    float a_pitch = absf(d_pitch);

    bool lr = a_roll  >= hb->lr_thresh_deg;
    bool fh = a_pitch >= hb->fh_thresh_deg;

    if (!lr && !fh) {
        return HB_STATE_BALANCED;
    }

    /* 哪个偏移更大就算哪一类（也可以以后细分“偏左/偏右”、“偏前/偏后”） */
    if (lr && (!fh || a_roll >= a_pitch)) {
        return HB_STATE_LR_IMBALANCE;
    } else {
        return HB_STATE_FH_IMBALANCE;
    }
}

hb_state_t horse_balance_update(horse_balance_t *hb,
                                float heading,
                                float roll,
                                float pitch,
                                bool *changed_if_nonnull)
{
    if (changed_if_nonnull) {
        *changed_if_nonnull = false;
    }

    /* 第一次调用：记录 baseline，不判定不报警 */
    if (!hb->baseline_set) {
        hb->heading0 = heading;
        hb->roll0    = roll;
        hb->pitch0   = pitch;
        hb->baseline_set = true;
        hb->state = HB_STATE_BALANCED;
        hb->last_change_ts = k_uptime_get_32();
        return hb->state;
    }

    float d_roll  = roll  - hb->roll0;
    float d_pitch = pitch - hb->pitch0;

    hb_state_t new_state = decide_state(hb, d_roll, d_pitch);

    if (new_state != hb->state) {
        hb->state = new_state;
        hb->last_change_ts = k_uptime_get_32();
        hb->last_roll  = roll;
        hb->last_pitch = pitch;
        if (changed_if_nonnull) {
            *changed_if_nonnull = true;
        }
    }

    return hb->state;
}