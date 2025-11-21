/*
 * Horse balance + BME280 demo
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <math.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#include "horse_balance.h"

/* ==================== BNO055（平衡仪）部分 ==================== */

#define BNO_ADDR        0x28   // 如果 ADR 接到 VDD 改 0x29
#define REG_CHIP_ID     0x00
#define REG_OPR_MODE    0x3D
#define REG_PWR_MODE    0x3E
#define MODE_CONFIG     0x00
#define MODE_NDOF       0x0C
#define REG_EUL_H_L     0x1A   // heading L, then roll, pitch

/* 在 overlay 里要有：
 * bno055: bno055@28 {
 *     compatible = "bosch,bno055";
 *     reg = <0x28>;
 *     ...
 * };
 */
static const struct i2c_dt_spec bno = I2C_DT_SPEC_GET(DT_NODELABEL(bno055));

static int wr8(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write_dt(&bno, buf, 2);
}

static int rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_read_dt(&bno, &reg, 1, buf, len);
}

/* ==================== BME280（温湿度/气压）部分 ==================== */

/* 从 devicetree 拿到我们在 overlay 里起名的 bme280 节点 */
#define BME280_NODE DT_NODELABEL(bme280)
static const struct device *const bme280_dev = DEVICE_DT_GET(BME280_NODE);
static const struct i2c_dt_spec bme_i2c = I2C_DT_SPEC_GET(BME280_NODE);

/* 把 struct sensor_value 打印成 “整数.三位小数” */
static void print_sensor_value(const char *name,
                               const struct sensor_value *val,
                               const char *unit)
{
    /* val1: 整数部分, val2: 小数部分(1e-6) */
    int32_t whole = val->val1;
    int32_t frac  = val->val2;  /* 10^-6 */

    /* 转成 milli 单位，方便格式化 */
    int32_t milli = whole * 1000 + frac / 1000;

    int32_t int_part  = milli / 1000;
    int32_t frac_part = milli % 1000;
    if (frac_part < 0) {
        frac_part = -frac_part;
    }

    LOG_INF("%s = %d.%03d %s", name, int_part, frac_part, unit);
}

/* ==================== 主程序 ==================== */

void main(void)
{
    /* -------- 1) 初始化 BNO055（平衡仪） -------- */
    if (!device_is_ready(bno.bus)) {
        LOG_ERR("I2C bus not ready");
        return;
    }

    uint8_t id = 0;
    if (rd(REG_CHIP_ID, &id, 1) || id != 0xA0) {
        LOG_ERR("BNO055 not found, id=0x%02X", id);
        return;
    }
    LOG_INF("BNO055 OK, id=0x%02X", id);

    /* 配置 -> 正常供电 -> NDOF */
    wr8(REG_OPR_MODE, MODE_CONFIG);
    k_msleep(20);
    wr8(REG_PWR_MODE, 0x00);
    k_msleep(10);
    wr8(REG_OPR_MODE, MODE_NDOF);
    k_msleep(50);

    /* ===== 初始化马背平衡监控 ===== */
    horse_balance_t hb;
    /* 阈值：左右 15°，前后 15°，你后面可以自己调 */
    horse_balance_init(&hb, 15.0f, 15.0f);

    LOG_INF("Horse balance monitor ready (LR>=%.1f deg, FH>=%.1f deg)",
            hb.lr_thresh_deg, hb.fh_thresh_deg);

    /* ===== 状态检测参数 ===== */
    bool first_sample = true;

    typedef enum {
        STATE_NORMAL = 0,
        STATE_LEFT,   // 向左倾
        STATE_RIGHT,  // 向右倾
        STATE_FRONT,  // 向前倾
        STATE_HIND    // 向后倾
    } balance_state_t;

    balance_state_t last_state = STATE_NORMAL;

    /* 连续超限计数（防抖） */
    uint8_t lr_over_cnt = 0;
    uint8_t fh_over_cnt = 0;
    int lr_dir = 0;  // -1 = 左, +1 = 右
    int fh_dir = 0;  // -1 = 前, +1 = 后

    const float LR_THRESH = 15.0f;   // 左右阈值
    const float FH_THRESH = 15.0f;   // 前后阈值

    /* 连续多少帧超限才判定失衡：10 帧 × 100ms ≈ 1s */
    const uint8_t MIN_SAMPLES = 10;

    /* -------- 2) 初始化 BME280（温湿度/气压） -------- */
    if (!device_is_ready(bme280_dev)) {
        LOG_WRN("BME280 device not ready (will skip env data)");
    } else {
        LOG_INF("BME280 ready");
    }

    struct sensor_value temp, hum, press;
    int ret;
    uint32_t last_bme_ms = 0;   // 上一次 BME280 采样时间（1s 一次）

    /* ==================== 主循环：100ms 一帧 ==================== */
    while (1) {
        /* -------- A) 读 BNO055，做平衡判定 -------- */
        uint8_t raw[6];

        if (!rd(REG_EUL_H_L, raw, sizeof(raw))) {
            int16_t heading_raw = (int16_t)((raw[1] << 8) | raw[0]);
            int16_t roll_raw    = (int16_t)((raw[3] << 8) | raw[2]);
            int16_t pitch_raw   = (int16_t)((raw[5] << 8) | raw[4]);

            float heading = heading_raw / 16.0f;
            float roll    = roll_raw    / 16.0f;
            float pitch   = pitch_raw   / 16.0f;
            (void)heading;  // 暂时不用 heading，防止编译 warning

            if (first_sample) {
                /* 第一次采样：记录 baseline，只打一次 “Normal (baseline set...)” */
                hb.roll0  = roll;
                hb.pitch0 = pitch;
                first_sample = false;
                last_state = STATE_NORMAL;

                LOG_INF("✅ Normal (baseline set: roll0=%.2f, pitch0=%.2f)",
                        hb.roll0, hb.pitch0);
            } else {
                /* 相对 baseline 的偏移 */
                float d_roll  = roll  - hb.roll0;   // 左右
                float d_pitch = pitch - hb.pitch0;  // 前后

                /* 本帧是否超限 */
                bool lr_over = fabsf(d_roll)  > LR_THRESH;
                bool fh_over = fabsf(d_pitch) > FH_THRESH;

                /* 左右方向计数 */
                if (lr_over) {
                    lr_dir = (d_roll < 0.0f) ? -1 : +1;  // 约定：负=左，正=右
                    if (lr_over_cnt < 255) {
                        lr_over_cnt++;
                    }
                } else {
                    lr_over_cnt = 0;
                }

                /* 前后方向计数 */
                if (fh_over) {
                    /* 约定：pitch < 0 = 偏前，pitch > 0 = 偏后 */
                    fh_dir = (d_pitch < 0.0f) ? -1 : +1;
                    if (fh_over_cnt < 255) {
                        fh_over_cnt++;
                    }
                } else {
                    fh_over_cnt = 0;
                }

                /* 根据“连续超限帧数 + 方向”决定当前状态 */
                balance_state_t cur_state = STATE_NORMAL;

                if (lr_over_cnt >= MIN_SAMPLES && lr_over_cnt >= fh_over_cnt) {
                    cur_state = (lr_dir < 0) ? STATE_LEFT : STATE_RIGHT;
                } else if (fh_over_cnt >= MIN_SAMPLES) {
                    cur_state = (fh_dir < 0) ? STATE_FRONT : STATE_HIND;
                }

                /* 在正常范围内，每帧打印一次 Normal */
                if (cur_state == STATE_NORMAL) {
                    LOG_INF("✅ Normal");
                }

                /* 只有“进入某个失衡状态”时，打一条 warning */
                if (cur_state != last_state && cur_state != STATE_NORMAL) {
                    switch (cur_state) {
                    case STATE_LEFT:
                        LOG_WRN("⚠️ Left–right imbalance: leaning left");
                        break;
                    case STATE_RIGHT:
                        LOG_WRN("⚠️ Left–right imbalance: leaning right");
                        break;
                    case STATE_FRONT:
                        LOG_WRN("⚠️ Front–hind imbalance: front-heavy (leaning forward)");
                        break;
                    case STATE_HIND:
                        LOG_WRN("⚠️ Front–hind imbalance: hind-heavy (leaning backward)");
                        break;
                    default:
                        break;
                    }
                }

                last_state = cur_state;
            }
        }

        /* -------- B) 每 1 秒跑一次 BME280 逻辑 -------- */
        if (device_is_ready(bme280_dev)) {
            uint32_t now = k_uptime_get_32();
            if (now - last_bme_ms >= 1000U) {   // 1 秒一次
                last_bme_ms = now;

                ret = sensor_sample_fetch(bme280_dev);
                if (ret) {
                    LOG_ERR("sensor_sample_fetch failed (%d)", ret);
                } else {
                    /* 温度 */
                    ret = sensor_channel_get(bme280_dev,
                                             SENSOR_CHAN_AMBIENT_TEMP,
                                             &temp);
                    if (ret == 0) {
                        print_sensor_value("Temperature", &temp, "degC");
                    } else {
                        LOG_ERR("Read temperature failed (%d)", ret);
                    }

                    /* 湿度 */
                    ret = sensor_channel_get(bme280_dev,
                                             SENSOR_CHAN_HUMIDITY,
                                             &hum);
                    if (ret == 0) {
                        print_sensor_value("Humidity", &hum, "%%");
                    } else {
                        LOG_ERR("Read humidity failed (%d)", ret);
                    }

                    /* 气压 */
                    ret = sensor_channel_get(bme280_dev,
                                             SENSOR_CHAN_PRESS,
                                             &press);
                    if (ret == 0) {
                        print_sensor_value("Pressure", &press, "kPa");
                    } else {
                        LOG_ERR("Read pressure failed (%d)", ret);
                    }
                }
            }
        }

        /* 平衡仪检测节奏：100 ms 一帧 */
        k_msleep(100);
    }
}
