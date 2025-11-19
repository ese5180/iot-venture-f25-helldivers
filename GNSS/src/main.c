/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Horse clip GNSS logic:
 * - RGB LED (using board LEDs) shows GNSS status.
 * - Button1 records trough (water) position once GNSS has a valid fix.
 * - When the horse stays near the trough > 3s, we treat it as a "water visit":
 *      water_visit_flag = 1;
 *      total stay time is accumulated.
 * - Periodically output GNSS status (including speed & heading) for anti-theft.
 * - After trough is set (NORMAL state):
 *      If 10 consecutive no-fix PVTs happen, treat GNSS as lost:
 *          -> log warning, restart GNSS, LED turns RED.
 *      When fix comes back:
 *          -> LED turns GREEN again, no need to re-mark trough.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gnss_sample, LOG_LEVEL_INF);

#include <zephyr/drivers/gpio.h>

#include <stdio.h>
#include <math.h>

#include <nrf_modem_gnss.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

/* ====================== 参数可调 ====================== */

/* 认为“在水槽附近”的半径 (m) */
#define TROUGH_RADIUS_M              10.0

/* 停留超过多少 ms 认为是一次喝水事件 */
#define WATER_MIN_DURATION_MS        3000

/* 防盗：多长时间输出一次 GNSS 状态 (ms) —— 这里是 5 秒 */
#define ANTI_THEFT_INTERVAL_MS       5000

/* 检测丢星：连续多少次没有 fix 判定为 GNSS 信号有问题 */
#define NO_FIX_THRESHOLD             10

/* 费城时区相对 GNSS UTC 的小时偏移（简单版：UTC-5） */
#define PHILLY_TIME_OFFSET_HOURS     (-5)

/* 是否打印 NMEA ($GPGGA / $GPGLL) 文本：
 * 0 = 不打印（现在默认关掉）
 * 1 = 打印
 */
#define ENABLE_NMEA_PRINT            0

/* ====================== GPIO / 硬件定义 ====================== */
/* 这里假设使用 nRF91 DK 的 LED0/LED1/LED2 作为 RGB，SW0 作为 Button1 */

/* LED */
#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)

/* Button1 */
#define BUTTON1_NODE   DT_ALIAS(sw0)

#if !DT_NODE_HAS_STATUS(LED_RED_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay)
#error "LED aliases (led0, led1, maybe led2) not found in devicetree"
#endif

#if !DT_NODE_HAS_STATUS(BUTTON1_NODE, okay)
#error "Button alias sw0 not found in devicetree"
#endif

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
#if DT_NODE_HAS_STATUS(LED_BLUE_NODE, okay)
static const struct gpio_dt_spec led_blue  = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);
#endif

static const struct gpio_dt_spec button1   = GPIO_DT_SPEC_GET(BUTTON1_NODE, gpios);
static struct gpio_callback button1_cb;

/* ====================== GNSS & 状态机定义 ====================== */

/* GNSS 状态（影响 LED 颜色与 Button 行为） */
enum gnss_state {
	GNSS_STATE_SEARCHING = 0,            /* 还没 fix -> 红色 */
	GNSS_STATE_WAIT_TROUGH_MARK,         /* 有 fix 等用户按键记水槽 -> 黄色 */
	GNSS_STATE_NORMAL,                   /* 已记水槽，正常跑逻辑 -> 绿色 */
};

static enum gnss_state current_state = GNSS_STATE_SEARCHING;

/* 最近一次的原始 PVT 数据（如有需要可直接使用） */
static struct nrf_modem_gnss_pvt_data_frame last_pvt;

/* PVT 到达的信号量（在主线程中处理） */
static K_SEM_DEFINE(pvt_data_sem, 0, 1);

/* 用于 NMEA（可选） */
K_MSGQ_DEFINE(nmea_queue, sizeof(struct nrf_modem_gnss_nmea_data_frame *), 8, 4);

/* poll 事件：PVT 信号量 + NMEA 队列 */
static struct k_poll_event events[2] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&pvt_data_sem, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&nmea_queue, 0),
};

/* 简化后的“当前 GNSS fix”结构 */
struct gnss_fix_simple {
	double lat;
	double lon;
	double alt;
	float  accuracy;
	bool   valid;

	/* GNSS 时间信息（UTC） */
	uint16_t year;
	uint16_t month;
	uint16_t day;
	uint16_t hour;
	uint16_t minute;
	uint16_t seconds;
	uint16_t ms;

	/* 运动信息 */
	float speed_mps;     /* 水平速度 (m/s) */
	float heading_deg;   /* 运动方向 / 航向 (deg) */
};

static struct gnss_fix_simple latest_fix;

/* 水槽位置 */
struct trough_position {
	double lat;
	double lon;
	double alt;
	float  accuracy;
	bool   valid;
};

static struct trough_position trough_pos = { 0 };

/* 用于检测“在水槽附近停留”的状态 */
struct water_visit_state {
	bool in_zone;                 /* 当前是否认为在水槽区域 */
	int64_t enter_uptime_ms;      /* 进入区域时的本地 uptime (ms) */
	struct gnss_fix_simple enter_fix; /* 进入时的 GNSS 时间/位置（用于记录开始时间） */
};

static struct water_visit_state water_state = { 0 };

/* 喝水统计：是否有过 visit，以及总停留时间累加 */
static int water_visit_flag = 0;

struct water_visit_stats {
	int64_t total_duration_ms;
};

static struct water_visit_stats water_stats = { 0 };

/* 防盗：最近一次上报时间（使用 uptime） */
static int64_t last_anti_theft_report_ms = 0;

/* 丢星检测：记录连续 no-fix 次数 & 是否处于 GNSS lost 状态 */
static int  no_fix_count = 0;
static bool gnss_lost    = false;

/* ====================== LED 控制 ====================== */

enum gps_led_color {
	GPS_LED_OFF = 0,
	GPS_LED_RED,
	GPS_LED_YELLOW,
	GPS_LED_GREEN,
};

static void gps_led_set(enum gps_led_color color)
{
	/* 如果你的 RGB 是外接的，只需要在这里改 pin 输出即可 */
	int red_on   = 0;
	int green_on = 0;
	int blue_on  = 0;

	switch (color) {
	case GPS_LED_RED:
		red_on = 1;
		break;
	case GPS_LED_YELLOW:
		red_on = 1;
		green_on = 1;
		break;
	case GPS_LED_GREEN:
		green_on = 1;
		break;
	case GPS_LED_OFF:
	default:
		break;
	}

	gpio_pin_set_dt(&led_red, red_on);
	gpio_pin_set_dt(&led_green, green_on);
#if DT_NODE_HAS_STATUS(LED_BLUE_NODE, okay)
	gpio_pin_set_dt(&led_blue, blue_on);
#endif
}

/* ====================== 工具函数：距离 & 时间偏移 ====================== */

static double deg2rad(double deg)
{
	const double pi = 3.14159265358979323846;
	return deg * pi / 180.0;
}

/* Haversine 公式计算两点之间距离 (米) */
static double distance_meters(double lat1, double lon1, double lat2, double lon2)
{
	double R = 6371000.0; /* 地球半径 (m) */
	double dlat = deg2rad(lat2 - lat1);
	double dlon = deg2rad(lon2 - lon1);

	double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
		   cos(deg2rad(lat1)) * cos(deg2rad(lat2)) *
		   sin(dlon / 2.0) * sin(dlon / 2.0);
	double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
	return R * c;
}

/* 把 GNSS UTC 小时转成“费城时间小时”（简单版：
 * 只做 hour + offset 的 0~23 wrap，日期暂不调整）
 */
static uint16_t utc_hour_to_philly(uint16_t utc_hour)
{
	int local_hour = (int)utc_hour + PHILLY_TIME_OFFSET_HOURS;

	while (local_hour < 0) {
		local_hour += 24;
	}
	while (local_hour >= 24) {
		local_hour -= 24;
	}
	return (uint16_t)local_hour;
}

static void print_satellite_stats(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	uint8_t tracked = 0, in_fix = 0, unhealthy = 0;

	for (int i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES; ++i) {
		if (pvt->sv[i].sv > 0) {
			tracked++;
			if (pvt->sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX) {
				in_fix++;
			}
			if (pvt->sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_UNHEALTHY) {
				unhealthy++;
			}
		}
	}

	printf("Tracking: %2d Using: %2d Unhealthy: %d\n", tracked, in_fix, unhealthy);
}

/* ====================== GNSS restart ====================== */

static void gnss_restart(void)
{
	int err;

	LOG_WRN("GNSS signal lost, restarting GNSS...");

	err = nrf_modem_gnss_stop();
	if (err) {
		LOG_ERR("nrf_modem_gnss_stop failed, err %d", err);
	}

	err = nrf_modem_gnss_start();
	if (err) {
		LOG_ERR("nrf_modem_gnss_start failed, err %d", err);
	}
}

/* ====================== 喝水事件统计 & 防盗上报 ====================== */

/* 喝水事件：进入水槽区域并停留超过 WATER_MIN_DURATION_MS
 * 这里只做统计：water_visit_flag=1，总时间累加
 */
static void report_water_visit(const struct gnss_fix_simple *start_fix,
			       int64_t duration_ms)
{
	water_visit_flag = 1;
	water_stats.total_duration_ms += duration_ms;

	uint16_t local_hour = utc_hour_to_philly(start_fix->hour);

	LOG_INF("WATER VISIT: duration = %lld ms, total = %lld ms, "
		"pos = (%f, %f), local time = %04u-%02u-%02u %02u:%02u:%02u.%03u (Philly)",
		(long long)duration_ms,
		(long long)water_stats.total_duration_ms,
		(double)start_fix->lat, (double)start_fix->lon,
		start_fix->year, start_fix->month, start_fix->day,
		local_hour, start_fix->minute, start_fix->seconds, start_fix->ms);

	/* TODO: 以后如果要发给主程序，可以在这里构建 message */
}

/* 防盗定位：周期性上报当前 GNSS 状态（时间 + 经纬度 + 高度 + 精度 + 速度 + 方向） */
static void report_anti_theft_status(const struct gnss_fix_simple *fix)
{
	uint16_t local_hour = utc_hour_to_philly(fix->hour);

	LOG_INF("ANTI-THEFT: local time = %04u-%02u-%02u %02u:%02u:%02u.%03u (Philly), "
		"pos = (%f, %f, alt=%f), acc = %.1f m, speed = %.2f m/s, heading = %.1f deg",
		fix->year, fix->month, fix->day,
		local_hour, fix->minute, fix->seconds, fix->ms,
		(double)fix->lat, (double)fix->lon, (double)fix->alt,
		(double)fix->accuracy,
		(double)fix->speed_mps, (double)fix->heading_deg);

	/* TODO: 以后在这里把这些数据打包丢给 LTE-M 线程 / 主程序。 */
}

/* ====================== 逻辑：水槽区域检测 ====================== */

static void water_zone_update(const struct gnss_fix_simple *fix)
{
	if (!trough_pos.valid) {
		return;
	}

	double d = distance_meters(fix->lat, fix->lon, trough_pos.lat, trough_pos.lon);
	bool now_in_zone = (d <= TROUGH_RADIUS_M);

	int64_t now_ms = k_uptime_get();

	if (!water_state.in_zone && now_in_zone) {
		/* 刚进入水槽区域 */
		water_state.in_zone = true;
		water_state.enter_uptime_ms = now_ms;
		water_state.enter_fix = *fix;

		LOG_INF("Enter trough zone, distance = %.2f m", d);
	} else if (water_state.in_zone && !now_in_zone) {
		/* 刚离开水槽区域 */
		int64_t duration_ms = now_ms - water_state.enter_uptime_ms;
		water_state.in_zone = false;

		LOG_INF("Leave trough zone, duration = %lld ms, distance at leave = %.2f m",
			(long long)duration_ms, d);

		if (duration_ms >= WATER_MIN_DURATION_MS) {
			/* 认为是一次喝水事件：设置 flag=1，并累计时间 */
			report_water_visit(&water_state.enter_fix, duration_ms);
		}
	}
}

/* ====================== 按键：用于标记水槽位置 ====================== */

static void on_button1_pressed(void)
{
	/* 只在有 fix 且还没记水槽位置时处理：
	 * 状态: WAIT_TROUGH_MARK, 并且 latest_fix.valid == true.
	 */
	if (current_state != GNSS_STATE_WAIT_TROUGH_MARK) {
		LOG_INF("Button1 pressed, but not in WAIT_TROUGH_MARK state");
		return;
	}

	if (!latest_fix.valid) {
		LOG_INF("Button1 pressed, but no valid fix yet");
		return;
	}

	/* 记录当前 fix 为水槽位置 */
	trough_pos.lat = latest_fix.lat;
	trough_pos.lon = latest_fix.lon;
	trough_pos.alt = latest_fix.alt;
	trough_pos.accuracy = latest_fix.accuracy;
	trough_pos.valid = true;

	current_state = GNSS_STATE_NORMAL;
	gps_led_set(GPS_LED_GREEN);

	LOG_INF("Trough position saved: (%f, %f, alt=%f), acc=%.1f m",
		(double)trough_pos.lat, (double)trough_pos.lon,
		(double)trough_pos.alt, (double)trough_pos.accuracy);
}

static void button1_isr(const struct device *dev,
			struct gpio_callback *cb,
			uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* 简单防抖：用 uptime 做 200ms 间隔 */
	static int64_t last_press_ms;
	int64_t now = k_uptime_get();

	if (now - last_press_ms < 200) {
		return;
	}
	last_press_ms = now;

	on_button1_pressed();
}

/* ====================== PVT 处理：更新状态机 + 功能逻辑 ====================== */

static void handle_pvt(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	bool fix_valid = (pvt->flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID);

	/* 没有 fix 的情况 */
	if (!fix_valid) {
		/* 如果我们已经在 NORMAL 且有水槽位置，才做“丢星检测” */
		if (current_state == GNSS_STATE_NORMAL && trough_pos.valid) {
			no_fix_count++;

			/* 到达阈值，认为 GNSS 信号有问题 */
			if (!gnss_lost && no_fix_count >= NO_FIX_THRESHOLD) {
				gnss_lost = true;
				gps_led_set(GPS_LED_RED);
				gnss_restart();
				/* 不在这里打印卫星信息，避免log 太吵 */
			}
		} else if (current_state == GNSS_STATE_SEARCHING) {
			/* 只在最开始 SEARCHING 阶段打印卫星信息 */
			print_satellite_stats(pvt);
		}
		return;
	}

	/* fix_valid == true */
	/* 一旦拿到 fix，就清零 no_fix_count */
	no_fix_count = 0;

	/* 如果之前处于 gnss_lost 状态，现在算是恢复了 */
	if (gnss_lost) {
		gnss_lost = false;

		if (trough_pos.valid) {
			/* 已经有水槽位置了，直接回到绿色 NORMAL 状态，不需要黄灯/重设水槽 */
			current_state = GNSS_STATE_NORMAL;
			gps_led_set(GPS_LED_GREEN);
		} else {
			/* 理论上不会发生：lost 时我们是在 NORMAL 且有 trough */
			LOG_WRN("GNSS fix restored but trough_pos invalid?");
		}

		LOG_INF("GNSS fix restored after loss");
	}

	/* 更新 latest_fix（UTC 时间） */
	latest_fix.lat      = pvt->latitude;
	latest_fix.lon      = pvt->longitude;
	latest_fix.alt      = pvt->altitude;
	latest_fix.accuracy = pvt->accuracy;
	latest_fix.valid    = true;

	latest_fix.year     = pvt->datetime.year;
	latest_fix.month    = pvt->datetime.month;
	latest_fix.day      = pvt->datetime.day;
	latest_fix.hour     = pvt->datetime.hour;     /* UTC 小时 */
	latest_fix.minute   = pvt->datetime.minute;
	latest_fix.seconds  = pvt->datetime.seconds;
	latest_fix.ms       = pvt->datetime.ms;

	/* 运动信息：速度和方向（heading） */
	latest_fix.speed_mps   = pvt->speed;   /* 单位：m/s */
	latest_fix.heading_deg = pvt->heading; /* 单位：度 */

	/* 第一次拿到 fix（从 SEARCHING 进来） -> 黄灯 + 等待用户设水槽 */
	if (current_state == GNSS_STATE_SEARCHING) {
		current_state = GNSS_STATE_WAIT_TROUGH_MARK;
		gps_led_set(GPS_LED_YELLOW);

		LOG_INF("Got first valid GNSS fix, waiting for trough mark (Button1)");
	}

	/* 当已经记录水槽位置并处于 NORMAL 状态时，才进行水槽区域检测等逻辑 */
	if (current_state == GNSS_STATE_NORMAL && trough_pos.valid) {
		/* 喝水区域进入/离开检测 */
		water_zone_update(&latest_fix);

		/* 防盗周期上报（现在是 5 秒一次） */
		int64_t now_ms = k_uptime_get();
		if (now_ms - last_anti_theft_report_ms >= ANTI_THEFT_INTERVAL_MS) {
			last_anti_theft_report_ms = now_ms;
			report_anti_theft_status(&latest_fix);
		}
	}
}

/* ====================== GNSS 事件回调（轻量） ====================== */

static void gnss_event_handler(int event)
{
	if (event == NRF_MODEM_GNSS_EVT_PVT) {
		if (nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt),
					NRF_MODEM_GNSS_DATA_PVT) == 0) {
			k_sem_give(&pvt_data_sem);
		}
	} else if (event == NRF_MODEM_GNSS_EVT_NMEA) {
		struct nrf_modem_gnss_nmea_data_frame *nmea =
			k_malloc(sizeof(struct nrf_modem_gnss_nmea_data_frame));
		if (!nmea) {
			return;
		}
		if (nrf_modem_gnss_read(nmea,
					sizeof(struct nrf_modem_gnss_nmea_data_frame),
					NRF_MODEM_GNSS_DATA_NMEA) == 0) {
			if (k_msgq_put(&nmea_queue, &nmea, K_NO_WAIT) != 0) {
				k_free(nmea);
			}
		} else {
			k_free(nmea);
		}
	}
}

/* ====================== GNSS 初始化启动 ====================== */

static int gnss_init_and_start(void)
{
	int err;

	/* GNSS 功能模式（仅 GNSS，不开 LTE） */
	err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
	if (err) {
		LOG_ERR("Failed to activate GNSS functional mode, err %d", err);
		return err;
	}

	err = nrf_modem_gnss_event_handler_set(gnss_event_handler);
	if (err) {
		LOG_ERR("Failed to set GNSS event handler, err %d", err);
		return err;
	}

	/* NMEA：控制 GPGGA/GPGLL 等输出类型的地方
	 *  - NRF_MODEM_GNSS_NMEA_GGA_MASK -> $GPGGA
	 *  - NRF_MODEM_GNSS_NMEA_GLL_MASK -> $GPGLL
	 *
	 * 如果你想完全关掉 NMEA，可以把 nmea_mask 设成 0，
	 * 或者直接注释掉 nrf_modem_gnss_nmea_mask_set 这一段。
	 */
	uint16_t nmea_mask = NRF_MODEM_GNSS_NMEA_GGA_MASK |
			     NRF_MODEM_GNSS_NMEA_GLL_MASK;
	err = nrf_modem_gnss_nmea_mask_set(nmea_mask);
	if (err) {
		LOG_ERR("Failed to set NMEA mask, err %d", err);
		return err;
	}

	/* 连续跟踪，1 秒一帧 */
	err = nrf_modem_gnss_fix_retry_set(0);
	if (err) {
		LOG_ERR("Failed to set fix retry, err %d", err);
		return err;
	}

	err = nrf_modem_gnss_fix_interval_set(1);
	if (err) {
		LOG_ERR("Failed to set fix interval, err %d", err);
		return err;
	}

	err = nrf_modem_gnss_start();
	if (err) {
		LOG_ERR("Failed to start GNSS, err %d", err);
		return err;
	}

	return 0;
}

/* ====================== 硬件初始化：LED + Button ====================== */

static int hardware_init(void)
{
	int err;

	/* LED */
	if (!device_is_ready(led_red.port) || !device_is_ready(led_green.port)) {
		LOG_ERR("LED device not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
	err = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
#if DT_NODE_HAS_STATUS(LED_BLUE_NODE, okay)
	if (!device_is_ready(led_blue.port)) {
		LOG_ERR("LED blue device not ready");
		return -ENODEV;
	}
	err = gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
#endif

	/* 开机默认红灯（还在搜星） */
	gps_led_set(GPS_LED_RED);

	/* Button1 */
	if (!device_is_ready(button1.port)) {
		LOG_ERR("Button1 device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&button1, GPIO_INPUT);
	if (err) {
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button1,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		return err;
	}

	gpio_init_callback(&button1_cb, button1_isr, BIT(button1.pin));
	gpio_add_callback(button1.port, &button1_cb);

	return 0;
}

/* ====================== main ====================== */

int main(void)
{
	int err;

	LOG_INF("Horse clip GNSS app starting...");

	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("nrf_modem_lib_init failed, err %d", err);
		return err;
	}

	err = hardware_init();
	if (err) {
		LOG_ERR("hardware_init failed, err %d", err);
		return err;
	}

	err = gnss_init_and_start();
	if (err) {
		LOG_ERR("gnss_init_and_start failed, err %d", err);
		return err;
	}

	while (1) {
		(void)k_poll(events, 2, K_FOREVER);

		/* 新 PVT 数据 */
		if (events[0].state == K_POLL_STATE_SEM_AVAILABLE &&
		    k_sem_take(events[0].sem, K_NO_WAIT) == 0) {
			handle_pvt(&last_pvt);
		}

		/* 新 NMEA（可选） */
		if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
			struct nrf_modem_gnss_nmea_data_frame *nmea;
			while (k_msgq_get(&nmea_queue, &nmea, K_NO_WAIT) == 0) {
#if ENABLE_NMEA_PRINT
				/* 这里会打印 $GPGGA / $GPGLL 等 NMEA 文本 */
				printf("%s", nmea->nmea_str);
#endif
				k_free(nmea);
			}
		}

		events[0].state = K_POLL_STATE_NOT_READY;
		events[1].state = K_POLL_STATE_NOT_READY;
	}
}
