#include "sensor.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <math.h>

LOG_MODULE_REGISTER(sensor_module, LOG_LEVEL_INF);

/* ====================== 全局共享数据 ====================== */
float g_temperature = 0;
float g_humidity    = 0;
float g_pressure    = 0;
float g_roll        = 0;
float g_pitch       = 0;
balance_state_t g_state = STATE_NORMAL;

typedef enum {
    HB_PHASE_BME_ONLY = 0,
    HB_PHASE_BNO_ONLY = 1,
} hb_phase_t;

static volatile hb_phase_t g_phase = HB_PHASE_BME_ONLY;

/* ====================== BNO055 ====================== */

#define REG_CHIP_ID     0x00
#define REG_OPR_MODE    0x3D
#define REG_PWR_MODE    0x3E
#define MODE_CONFIG     0x00
#define MODE_NDOF       0x0C
#define REG_EUL_H_L     0x1A

static const struct i2c_dt_spec bno = I2C_DT_SPEC_GET(DT_NODELABEL(bno055));

static int bno_wr8(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write_dt(&bno, buf, sizeof(buf));
}

static int bno_rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_read_dt(&bno, &reg, 1, buf, len);
}

/* ====================== BME280 ====================== */

#define BME280_NODE DT_NODELABEL(bme280)
static const struct device *const bme280_dev = DEVICE_DT_GET(BME280_NODE);

/* ====================== 电源控制 ====================== */

#define BNO_PWR_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec bno_pwr =
    GPIO_DT_SPEC_GET(BNO_PWR_NODE, bno_pwr_gpios);

static void bno_power(bool on)
{
    gpio_pin_set_dt(&bno_pwr, on ? 1 : 0);
}

/* ====================== BME线程 ====================== */

static void bme280_thread(void *p1, void *p2, void *p3)
{
    struct sensor_value temp, hum, press;
    int ret;

    while (1) {

        if (g_phase != HB_PHASE_BME_ONLY) {
            k_sleep(K_MSEC(100));
            continue;
        }

        ret = sensor_sample_fetch(bme280_dev);

        if (ret == 0) {

            sensor_channel_get(bme280_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
            sensor_channel_get(bme280_dev, SENSOR_CHAN_HUMIDITY, &hum);
            sensor_channel_get(bme280_dev, SENSOR_CHAN_PRESS, &press);

            g_temperature = temp.val1 + temp.val2 / 1e6;
            g_humidity    = hum.val1  + hum.val2  / 1e6;
            g_pressure    = press.val1 + press.val2 / 1e6;
        }

        k_sleep(K_SECONDS(1));
    }
}

/* ====================== BNO线程 ====================== */

static void bno055_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("BNO055 thread start");

    while (1) {

        /* 需要使用 BNO → 上电 */
        bno_power(true);
        k_msleep(700);

        /* 读 CHIP ID */
        uint8_t id = 0;
        int ret = bno_rd(REG_CHIP_ID, &id, 1);
        if (ret || id != 0xA0) {
            LOG_ERR("BNO055 CHIP_ID error ret=%d id=0x%02X", ret, id);
            bno_power(false);
            k_sleep(K_SECONDS(2));
            continue;
        }

        /* 初始化模式 */
        bno_wr8(REG_OPR_MODE, MODE_CONFIG);
        k_msleep(20);
        bno_wr8(REG_PWR_MODE, 0x00);
        k_msleep(10);
        bno_wr8(REG_OPR_MODE, MODE_NDOF);
        k_msleep(50);

        /* ====== 马背平衡监测逻辑 ====== */
        const float LR_THRESH     = 15.0f;
        const float FH_THRESH     = 15.0f;
        const uint8_t MIN_SAMPLES = 10;

        bool first_sample = true;
        balance_state_t last_state = STATE_NORMAL;

        float roll0  = 0.0f;
        float pitch0 = 0.0f;

        uint8_t lr_over_cnt = 0;
        uint8_t fh_over_cnt = 0;
        int lr_dir = 0;
        int fh_dir = 0;

        /* 开始采样 */
        while (g_phase == HB_PHASE_BNO_ONLY) {
            uint8_t raw[6];

            ret = bno_rd(REG_EUL_H_L, raw, sizeof(raw));
            if (ret) {
                LOG_ERR("BNO055 read EUL failed (%d), break", ret);
                break;
            }

            int16_t roll_raw  = (int16_t)((raw[3] << 8) | raw[2]);
            int16_t pitch_raw = (int16_t)((raw[5] << 8) | raw[4]);

            float roll  = roll_raw  / 16.0f;
            float pitch = pitch_raw / 16.0f;

            g_roll  = roll;
            g_pitch = pitch;

            if (first_sample) {
                roll0  = roll;
                pitch0 = pitch;
                first_sample = false;
                last_state = STATE_NORMAL;
            } else {
                float d_roll  = roll  - roll0;
                float d_pitch = pitch - pitch0;

                bool lr_over = fabsf(d_roll)  > LR_THRESH;
                bool fh_over = fabsf(d_pitch) > FH_THRESH;

                if (lr_over) {
                    lr_dir = (d_roll < 0.0f) ? -1 : +1;
                    if (lr_over_cnt < 255) lr_over_cnt++;
                } else lr_over_cnt = 0;

                if (fh_over) {
                    fh_dir = (d_pitch < 0.0f) ? -1 : +1;
                    if (fh_over_cnt < 255) fh_over_cnt++;
                } else fh_over_cnt = 0;

                balance_state_t cur_state = STATE_NORMAL;

                if (lr_over_cnt >= MIN_SAMPLES &&
                    lr_over_cnt >= fh_over_cnt) {
                    cur_state = (lr_dir < 0) ? STATE_LEFT : STATE_RIGHT;
                }
                else if (fh_over_cnt >= MIN_SAMPLES) {
                    cur_state = (fh_dir < 0) ? STATE_FRONT : STATE_HIND;
                }

                g_state = cur_state;
                last_state = cur_state;
            }

            k_msleep(100);
        }

        LOG_INF("BNO session done, powering off...");
        bno_power(false);

        k_msleep(500);
    }
}


/* ====================== 调度线程 ====================== */

static void scheduler_thread(void *p1, void *p2, void *p3)
{
    while (1) {

        g_phase = HB_PHASE_BME_ONLY;
        bno_power(false);
        k_sleep(K_SECONDS(5));

        bno_power(true);
        g_phase = HB_PHASE_BNO_ONLY;
        k_sleep(K_SECONDS(10));
    }
}

/* ====================== 线程创建 ====================== */

K_THREAD_DEFINE(bme280_thread_id, 2048, bme280_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(bno055_thread_id, 2048, bno055_thread, NULL, NULL, NULL, 4, 0, 0);
K_THREAD_DEFINE(scheduler_thread_id, 1024, scheduler_thread, NULL, NULL, NULL, 3, 0, 0);

/* ====================== 对外接口 ====================== */

void sensor_init(void)
{
    gpio_pin_configure_dt(&bno_pwr, GPIO_OUTPUT_INACTIVE);
}

float sensor_get_temperature(void) { return g_temperature; }
float sensor_get_humidity(void)    { return g_humidity; }
float sensor_get_pressure(void)    { return g_pressure; }
float sensor_get_roll(void)        { return g_roll; }
float sensor_get_pitch(void)       { return g_pitch; }
balance_state_t sensor_get_state(void) { return g_state; }
