/*
 * Horse balance (BNO055) + BME280 env sensor
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <math.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* ==================== BNO055（平衡仪）部分 ==================== */

#define REG_CHIP_ID     0x00
#define REG_OPR_MODE    0x3D
#define REG_PWR_MODE    0x3E
#define MODE_CONFIG     0x00
#define MODE_NDOF       0x0C
#define REG_EUL_H_L     0x1A   /* heading L, then roll, pitch */

/* overlay 里要有：
 * &i2c2 {
 *     status = "okay";
 *
 *     bno055: bno055@28 {
 *         compatible = "bosch,bno055";
 *         reg = <0x28>;
 *         label = "BNO055";
 *         status = "okay";
 *     };
 * };
 */
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

/* ==================== BME280（温湿度 / 气压）部分 ==================== */

/* overlay 里要有：
 *     bme280: bme280@77 {
 *         compatible = "bosch,bme280";
 *         reg = <0x77>;
 *         label = "BME280";
 *         status = "okay";
 *     };
 */
#define BME280_NODE DT_NODELABEL(bme280)
static const struct device *const bme280_dev = DEVICE_DT_GET(BME280_NODE);

/* ==================== BNO055 电源控制（P-MOS 高边）==================== */
/* overlay 里要有：
 *
 * / {
 *     zephyr,user {
 *         bno-pwr-gpios = <&gpio0 2 GPIO_ACTIVE_LOW>;
 *     };
 * };
 *
 * Gate 接的是 P0.02，通过 P-MOS 给 BNO055 的 VCC 供电：
 *  - ACTIVE_LOW: 写 1 = 有效 = 输出低电平 -> P-MOS 打开 -> BNO 有电
 *  - 写 0 = INACTIVE = 输出高电平 -> P-MOS 关断 -> BNO 断电
 */

#define BNO_PWR_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec bno_pwr =
	GPIO_DT_SPEC_GET(BNO_PWR_NODE, bno_pwr_gpios);

static void bno_power(bool on)
{
	/* on == true  -> 逻辑 1(Active) -> 输出低 -> P-MOS 开 -> BNO 有电
	 * on == false -> 逻辑 0(Inactive) -> 输出高 -> P-MOS 关 -> BNO 断电
	 */
	if (!device_is_ready(bno_pwr.port)) {
		LOG_ERR("BNO power GPIO device not ready");
		return;
	}

	gpio_pin_set_dt(&bno_pwr, on ? 1 : 0);
}

/* ==================== 5s / 10s 调度阶段定义 ==================== */

typedef enum {
	HB_PHASE_BME_ONLY = 0,   /* 只读 BME280，BNO 断电 */
	HB_PHASE_BNO_ONLY = 1,   /* 只读 BNO055，BME 暂停 */
} hb_phase_t;

/* 被多个线程访问，用 volatile 简单处理一下 */
static volatile hb_phase_t g_phase = HB_PHASE_BME_ONLY;

/* 把 struct sensor_value 转成 "整数.三位小数" 打印 */
static void print_sensor_value(const char *name,
			       const struct sensor_value *val,
			       const char *unit)
{
	/* val1: 整数部分, val2: 小数部分(1e-6) */
	int32_t whole = val->val1;
	int32_t frac  = val->val2; /* 10^-6 */

	int32_t milli = whole * 1000 + frac / 1000;

	int32_t int_part  = milli / 1000;
	int32_t frac_part = milli % 1000;
	if (frac_part < 0) {
		frac_part = -frac_part;
	}

	LOG_INF("%s = %d.%03d %s", name, int_part, frac_part, unit);
}

/* ==================== BME280 线程 ==================== */

static void bme280_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(bme280_dev)) {
		LOG_ERR("BME280 device not ready");
		return;
	}

	LOG_INF("BME280 thread start");

	struct sensor_value temp, hum, press;
	int ret;

	while (1) {

		/* 如果当前不是 BME-only 阶段，就啥也不干，小睡一下 */
		if (g_phase != HB_PHASE_BME_ONLY) {
			k_sleep(K_MSEC(100));
			continue;
		}

		/* 触发采样 */
		ret = sensor_sample_fetch(bme280_dev);
		if (ret) {
			LOG_ERR("BME280: sensor_sample_fetch failed (%d)", ret);
			k_sleep(K_SECONDS(1));
			continue;
		}

		/* 温度 */
		ret = sensor_channel_get(bme280_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (ret == 0) {
			print_sensor_value("Temperature", &temp, "degC");
		} else {
			LOG_ERR("Read temperature failed (%d)", ret);
		}

		/* 湿度 */
		ret = sensor_channel_get(bme280_dev, SENSOR_CHAN_HUMIDITY, &hum);
		if (ret == 0) {
			print_sensor_value("Humidity", &hum, "%%");
		} else {
			LOG_ERR("Read humidity failed (%d)", ret);
		}

		/* 气压 */
		ret = sensor_channel_get(bme280_dev, SENSOR_CHAN_PRESS, &press);
		if (ret == 0) {
			print_sensor_value("Pressure", &press, "kPa");
		} else {
			LOG_ERR("Read pressure failed (%d)", ret);
		}

		/* 每秒跑一次（只在 BME-only 阶段才会进来） */
		k_sleep(K_SECONDS(1));
	}
}

/* 线程创建：栈 2048、优先级 5（比 BNO 线程低一点） */
K_THREAD_DEFINE(bme280_thread_id,
		2048,
		bme280_thread,
		NULL, NULL, NULL,
		5, 0, 0);

/* ==================== BNO055：马背平衡线程 ==================== */

typedef enum {
	STATE_NORMAL = 0,
	STATE_LEFT,   /* 向左倾 */
	STATE_RIGHT,  /* 向右倾 */
	STATE_FRONT,  /* 向前倾 */
	STATE_HIND    /* 向后倾 */
} balance_state_t;

static void bno055_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!device_is_ready(bno.bus)) {
		LOG_ERR("I2C bus for BNO055 not ready");
		return;
	}

	while (1) {

		/* 等待到 “BNO-only 阶段” 才工作 */
		if (g_phase != HB_PHASE_BNO_ONLY) {
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_INF("BNO phase: power-up & init...");

		/* 给 BNO 一点上电时间（主线程已经把 P-MOS 打开） */
		k_msleep(700);

		/* 读 CHIP ID 确认芯片在总线上 */
		uint8_t id = 0;
		int ret = bno_rd(REG_CHIP_ID, &id, 1);
		if (ret || id != 0xA0) {
			LOG_ERR("BNO055 CHIP_ID failed, ret=%d id=0x%02X", ret, id);
			/* 这次 10s 周期就算了，等下一个周期再试 */
			k_sleep(K_SECONDS(1));
			continue;
		}
		LOG_INF("BNO055 OK, id=0x%02X", id);

		/* 配置 BNO055：进入配置模式 -> 正常供电 -> NDOF */
		ret = bno_wr8(REG_OPR_MODE, MODE_CONFIG);
		if (ret) {
			LOG_ERR("BNO055 set CONFIG mode failed (%d)", ret);
			continue;
		}
		k_msleep(20);

		ret = bno_wr8(REG_PWR_MODE, 0x00);
		if (ret) {
			LOG_ERR("BNO055 set PWR_MODE failed (%d)", ret);
			continue;
		}
		k_msleep(10);

		ret = bno_wr8(REG_OPR_MODE, MODE_NDOF);
		if (ret) {
			LOG_ERR("BNO055 set NDOF mode failed (%d)", ret);
			continue;
		}
		k_msleep(50);

		/* ====== 马背平衡监控参数 ====== */

		const float LR_THRESH     = 15.0f; /* 左右阈值 (deg) */
		const float FH_THRESH     = 15.0f; /* 前后阈值 (deg) */
		const uint8_t MIN_SAMPLES = 10;    /* 连续多少帧超限才认为失衡 ≈ 1s */

		bool first_sample = true;
		balance_state_t last_state = STATE_NORMAL;

		/* baseline：第一次读到的 roll / pitch 作为 0 点 */
		float roll0  = 0.0f;
		float pitch0 = 0.0f;

		/* 去抖计数 + 方向 */
		uint8_t lr_over_cnt = 0;
		uint8_t fh_over_cnt = 0;
		int lr_dir = 0;  /* -1=左, +1=右 */
		int fh_dir = 0;  /* -1=前, +1=后 */

		/* 在当前这个 “BNO-only 10 秒窗口” 内循环采样，
		 * 当 main 把 g_phase 切回 BME_ONLY 时，就会跳出这个循环。
		 */
		while (g_phase == HB_PHASE_BNO_ONLY) {
			uint8_t raw[6];

			ret = bno_rd(REG_EUL_H_L, raw, sizeof(raw));
			if (ret) {
				LOG_ERR("BNO055 read EUL failed (%d)", ret);
				k_msleep(100);
				continue;
			}

			int16_t heading_raw = (int16_t)((raw[1] << 8) | raw[0]);
			int16_t roll_raw    = (int16_t)((raw[3] << 8) | raw[2]);
			int16_t pitch_raw   = (int16_t)((raw[5] << 8) | raw[4]);

			float heading = heading_raw / 16.0f;
			float roll    = roll_raw    / 16.0f;
			float pitch   = pitch_raw   / 16.0f;
			(void)heading; /* 暂时不用 heading，避免编译警告 */

			if (first_sample) {
				/* 第一次采样：记录基准，只打印一次 baseline */
				roll0  = roll;
				pitch0 = pitch;
				first_sample = false;
				last_state = STATE_NORMAL;

				LOG_INF("Normal baseline set: roll0=%.2f, pitch0=%.2f",
					(double)roll0, (double)pitch0);
			} else {
				/* 相对 baseline 的偏移 */
				float d_roll  = roll  - roll0;   /* 左右 */
				float d_pitch = pitch - pitch0;  /* 前后 */

				bool lr_over = fabsf(d_roll)  > LR_THRESH;
				bool fh_over = fabsf(d_pitch) > FH_THRESH;

				/* 左右方向的“连续超限”计数 */
				if (lr_over) {
					lr_dir = (d_roll < 0.0f) ? -1 : +1;
					if (lr_over_cnt < 255) {
						lr_over_cnt++;
					}
				} else {
					lr_over_cnt = 0;
				}

				/* 前后方向的“连续超限”计数 */
				if (fh_over) {
					fh_dir = (d_pitch < 0.0f) ? -1 : +1;
					if (fh_over_cnt < 255) {
						fh_over_cnt++;
					}
				} else {
					fh_over_cnt = 0;
				}

				/* 根据连续计数 + 方向 来判定当前状态 */
				balance_state_t cur_state = STATE_NORMAL;

				if (lr_over_cnt >= MIN_SAMPLES &&
				    lr_over_cnt >= fh_over_cnt) {
					cur_state = (lr_dir < 0) ? STATE_LEFT : STATE_RIGHT;
				} else if (fh_over_cnt >= MIN_SAMPLES) {
					cur_state = (fh_dir < 0) ? STATE_FRONT : STATE_HIND;
				}

				/* 在正常范围内，每帧打一条 Normal（你可以以后改成降低频率） */
				if (cur_state == STATE_NORMAL) {
					LOG_INF("Normal");
				}

				/* 只有进入某个失衡状态时，打一条 warning */
				if (cur_state != last_state && cur_state != STATE_NORMAL) {
					switch (cur_state) {
					case STATE_LEFT:
						LOG_WRN("Left–right imbalance: leaning left");
						break;
					case STATE_RIGHT:
						LOG_WRN("Left–right imbalance: leaning right");
						break;
					case STATE_FRONT:
						LOG_WRN("Front–hind imbalance: front-heavy (leaning forward)");
						break;
					case STATE_HIND:
						LOG_WRN("Front–hind imbalance: hind-heavy (leaning backward)");
						break;
					default:
						break;
					}
				}

				last_state = cur_state;
			}

			/* 100 ms 一帧 */
			k_msleep(100);
		}

		LOG_INF("BNO phase ended, waiting for next cycle...");
	}
}

/* BNO055 线程：栈 2048、优先级 4（比 BME 高一点） */
K_THREAD_DEFINE(bno055_thread_id,
		2048,
		bno055_thread,
		NULL, NULL, NULL,
		4, 0, 0);

/* ==================== main：做 5s / 10s 调度 ==================== */

void main(void)
{
	int ret;

	LOG_INF("Horse balance (BNO055) + BME280 app start");

	/* 配置 BNO 电源控制 GPIO：初始先关 */
	if (!device_is_ready(bno_pwr.port)) {
		LOG_ERR("BNO power GPIO device not ready");
		return;
	}

	ret = gpio_pin_configure_dt(&bno_pwr, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		LOG_ERR("Failed to config BNO power GPIO (%d)", ret);
		return;
	}

	/* 初始进入 BME-only 阶段，BNO 断电 */
	g_phase = HB_PHASE_BME_ONLY;
	bno_power(false);
	LOG_INF("Start in BME-only phase");

	while (1) {
		/* 1) 先 5 秒：只读温湿度（BME-only） */
		g_phase = HB_PHASE_BME_ONLY;
		bno_power(false);
		LOG_INF("Phase: BME-only for 5 seconds");
		k_sleep(K_SECONDS(5));

		/* 2) 再 10 秒：只读平衡仪（BNO-only） */
		bno_power(true);
		g_phase = HB_PHASE_BNO_ONLY;
		LOG_INF("Phase: BNO-only for 10 seconds");
		k_sleep(K_SECONDS(10));
	}
}
