/*
 * Horse balance (BNO055) + BME280 env sensor
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <math.h>
#include "sensor_task.h"

static sensor_data_msg_t sys;
K_MUTEX_DEFINE(sensor_lock);
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/****************************************
 * BNO055 I2C
 ****************************************/

#define REG_EUL_H_L     0x1A

static const struct i2c_dt_spec bno = I2C_DT_SPEC_GET(DT_NODELABEL(bno055));

static int bno_rd(uint8_t reg, uint8_t *buf, size_t len)
{
	return i2c_write_read_dt(&bno, &reg, 1, buf, len);
}

/****************************************
 * BME280 设备
 ****************************************/

#define BME280_NODE DT_NODELABEL(bme280)
static const struct device *const bme280_dev = DEVICE_DT_GET(BME280_NODE);

/****************************************
 * BME280 线程
 ****************************************/

static void bme280_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	if (!device_is_ready(bme280_dev)) {
		LOG_ERR("BME280 device not ready");
		return;
	}

	struct sensor_value temp, hum, press;
	int ret;

	while (1) {

		ret = sensor_sample_fetch(bme280_dev);
		if (ret) {
			LOG_ERR("BME280 fetch failed %d", ret);
			k_sleep(K_SECONDS(1));
			continue;
		}

		sensor_channel_get(bme280_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(bme280_dev, SENSOR_CHAN_HUMIDITY,     &hum);
		sensor_channel_get(bme280_dev, SENSOR_CHAN_PRESS,        &press);

		float t = temp.val1 + temp.val2 / 1000000.0f;
		float h = hum.val1  + hum.val2  / 1000000.0f;
		float p = press.val1 + press.val2 / 1000000.0f;

		k_mutex_lock(&sensor_lock, K_FOREVER);
		sys.temperature = t;
		sys.humidity    = h;
		sys.pressure    = p;
		k_mutex_unlock(&sensor_lock);

		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(bme280_thread_id,
		2048,
		bme280_thread,
		NULL, NULL, NULL,
		5, 0, 0);

/****************************************
 * BNO055 线程
 ****************************************/

static void bno055_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	uint8_t raw[6];

	while (1) {

		int ret = bno_rd(REG_EUL_H_L, raw, sizeof(raw));
		if (ret) {
			LOG_ERR("BNO055 read failed %d", ret);
			k_msleep(100);
			continue;
		}

		int16_t heading_raw = (int16_t)((raw[1] << 8) | raw[0]);
		int16_t roll_raw    = (int16_t)((raw[3] << 8) | raw[2]);
		int16_t pitch_raw   = (int16_t)((raw[5] << 8) | raw[4]);

		float heading = heading_raw / 16.0f;
		float roll    = roll_raw    / 16.0f;
		float pitch   = pitch_raw   / 16.0f;

		k_mutex_lock(&sensor_lock, K_FOREVER);
		sys.bno_heading = heading;
		sys.bno_roll    = roll;
		sys.bno_pitch   = pitch;
		k_mutex_unlock(&sensor_lock);

		k_msleep(100);
	}
}

K_THREAD_DEFINE(bno055_thread_id,
		2048,
		bno055_thread,
		NULL, NULL, NULL,
		4, 0, 0);

/****************************************
 * API：main 调用
 ****************************************/

void sensor_task_start(void)
{
	LOG_INF("sensor_task started");
}

void sensor_task_get_data(sensor_data_msg_t *out)
{
    k_mutex_lock(&sensor_lock, K_FOREVER);
    *out = sys;
    k_mutex_unlock(&sensor_lock);
}
