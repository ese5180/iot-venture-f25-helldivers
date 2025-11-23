/* tests/balance/src/main.c */
#include <zephyr/ztest.h>
#include "horse_balance.h"
#include <math.h>

/* 简单的 float 比较 */
static inline void expect_float_eq(float a, float b, float eps, const char *msg)
{
	zassert_true(fabsf(a - b) < eps, "%s (a=%f, b=%f)", msg, (double)a, (double)b);
}

/* 1. 初始化后字段状态是否正确 */
ZTEST(horse_balance, test_init_sets_fields)
{
	horse_balance_t hb;

	horse_balance_init(&hb, 10.0f, 20.0f);

	zassert_false(hb.baseline_set, "baseline_set should be false after init");
	expect_float_eq(hb.heading0, 0.0f, 1e-6f, "heading0 must be 0");
	expect_float_eq(hb.roll0,    0.0f, 1e-6f, "roll0 must be 0");
	expect_float_eq(hb.pitch0,   0.0f, 1e-6f, "pitch0 must be 0");

	expect_float_eq(hb.lr_thresh_deg, 10.0f, 1e-6f, "lr_thresh_deg mismatch");
	expect_float_eq(hb.fh_thresh_deg, 20.0f, 1e-6f, "fh_thresh_deg mismatch");

	zassert_equal(hb.state, HB_STATE_BALANCED, "initial state must be BALANCED");
	zassert_equal(hb.last_roll,  0.0f, "last_roll must be 0 after init");
	zassert_equal(hb.last_pitch, 0.0f, "last_pitch must be 0 after init");
}

/* 2. 第一次 update：只设置 baseline，不触发变化，不告警 */
ZTEST(horse_balance, test_first_update_sets_baseline_only)
{
	horse_balance_t hb;
	bool changed = true;

	horse_balance_init(&hb, 10.0f, 10.0f);

	hb_state_t st = horse_balance_update(&hb,
					     30.0f, /* heading */
					     1.0f,  /* roll */
					     2.0f,  /* pitch */
					     &changed);

	zassert_true(hb.baseline_set, "baseline should be set after first update");
	expect_float_eq(hb.roll0,  1.0f, 1e-6f, "baseline roll0 mismatch");
	expect_float_eq(hb.pitch0, 2.0f, 1e-6f, "baseline pitch0 mismatch");

	zassert_equal(st, HB_STATE_BALANCED, "first update must stay BALANCED");
	zassert_false(changed, "changed flag must stay false on first update");
	zassert_false(horse_balance_is_warning(&hb),
		      "first update must not be treated as warning");
}

/* 3. roll 超过左右阈值 -> 左右不平衡 (LR_IMBALANCE) */
ZTEST(horse_balance, test_lr_imbalance_when_roll_exceeds_threshold)
{
	horse_balance_t hb;
	bool changed = false;

	horse_balance_init(&hb, 10.0f, 30.0f);

	/* 先设 baseline 在 (0,0,0) */
	horse_balance_update(&hb, 0.0f, 0.0f, 0.0f, NULL);

	/* roll 偏 15 度，超过 10 度阈值，应该判为 LR_IMBALANCE */
	hb_state_t st = horse_balance_update(&hb,
					     0.0f,
					     15.0f,   /* d_roll = +15 */
					     0.0f,
					     &changed);

	zassert_equal(st, HB_STATE_LR_IMBALANCE,
		      "state should be LR_IMBALANCE when roll exceeds threshold");
	zassert_true(changed, "changed flag must be true when state changes");
	zassert_true(horse_balance_is_warning(&hb),
		     "LR_IMBALANCE should be treated as warning");
}

/* 4. pitch 超过前后阈值 -> 前后不平衡 (FH_IMBALANCE) */
ZTEST(horse_balance, test_fh_imbalance_when_pitch_exceeds_threshold)
{
	horse_balance_t hb;
	bool changed = false;

	horse_balance_init(&hb, 30.0f, 10.0f);

	/* baseline (0,0,0) */
	horse_balance_update(&hb, 0.0f, 0.0f, 0.0f, NULL);

	/* pitch 偏 -20 度，超过 10 度阈值，应该判为 FH_IMBALANCE */
	hb_state_t st = horse_balance_update(&hb,
					     0.0f,
					     0.0f,
					     -20.0f,
					     &changed);

	zassert_equal(st, HB_STATE_FH_IMBALANCE,
		      "state should be FH_IMBALANCE when pitch exceeds threshold");
	zassert_true(changed, "changed flag must be true when state changes");
	zassert_true(horse_balance_is_warning(&hb),
		     "FH_IMBALANCE should be treated as warning");
}

/* 5. 从失衡回到正常：状态要回到 BALANCED，changed 要变 true */
ZTEST(horse_balance, test_back_to_balanced_resets_state)
{
	horse_balance_t hb;
	bool changed = false;

	horse_balance_init(&hb, 10.0f, 10.0f);

	/* baseline (0,0,0) */
	horse_balance_update(&hb, 0.0f, 0.0f, 0.0f, NULL);

	/* 先制造左右不平衡 */
	horse_balance_update(&hb, 0.0f, 15.0f, 0.0f, &changed);
	zassert_equal(hb.state, HB_STATE_LR_IMBALANCE,
		      "should be LR_IMBALANCE after large roll");

	/* 再回到接近 baseline，低于阈值 */
	changed = false;
	hb_state_t st = horse_balance_update(&hb,
					     0.0f,
					     2.0f,  /* 小偏移 */
					     1.0f,
					     &changed);

	zassert_equal(st, HB_STATE_BALANCED,
		      "state should return to BALANCED when offsets go below threshold");
	zassert_true(changed, "changed must be true when returning to BALANCED");
	zassert_false(horse_balance_is_warning(&hb),
		      "BALANCED state must not be warning");
}

/* 注册测试套件：名字叫 horse_balance */
ZTEST_SUITE(horse_balance, NULL, NULL, NULL, NULL, NULL);
