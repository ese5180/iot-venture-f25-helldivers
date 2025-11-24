/* gnss_task.c
 *
 * Implements GNSS + water-intake logic as a Zephyr thread.
 * - Uses GPS to track position, speed, heading.
 * - Button marks trough (water) position once.
 * - If horse stays near trough > 3s, counts as water visit (accumulates time).
 * - Every PVT event, this thread sends one gnss_status_msg to gnss_msgq:
 *      * if fix_valid == true: update latest_fix, then send.
 *      * if fix_valid == false: keep last valid latest_fix, still send.
 * - First time trough is marked, we send one message with is_water_gnss = true.
 * - If GNSS is considered lost (10 consecutive no-fix), status = SIGNAL_LOST,
 *   we keep sending last-known position until fix is restored.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gnss_task, LOG_LEVEL_INF);

#include <zephyr/drivers/gpio.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include <nrf_modem_gnss.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

#include "gnss_task.h"

/* ====================== 参数可调 ====================== */

/* 认为“在水槽附近”的半径 (m) */
#define TROUGH_RADIUS_M              10.0

/* 停留超过多少 ms 认为是一次喝水事件 */
#define WATER_MIN_DURATION_MS        3000

/* 检测丢星：连续多少次没有 fix 判定为 GNSS 信号有问题 */
#define NO_FIX_THRESHOLD             10

/* 费城时区相对 GNSS UTC 的小时偏移（简单版：UTC-5） */
#define PHILLY_TIME_OFFSET_HOURS     (-5)

/* GNSS 任务线程配置 */
#define GNSS_TASK_STACK_SIZE         4096
#define GNSS_TASK_PRIORITY           2

/* ====================== GPIO / 硬件定义 ====================== */
/* 这里假设使用 nRF91 DK 的 LED0/LED1 作为指示灯，SW0 作为 Button1 */

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

/* ====================== 在 LTE ready 之后启动 GNSS ====================== */

void gnss_start_after_lte_ready(void)
{
    int err;

    /* 激活 GNSS 功能模式（在已有 LTE 的基础上打开 GNSS） */
    err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
    if (err) {
        printk("Failed to activate GNSS functional mode: %d\n", err);
        return;
    }

    /* 现在 GNSS 功能模式已打开，再配置 fix_retry / fix_interval */
    err = nrf_modem_gnss_fix_retry_set(0);
    if (err) {
        printk("Failed to set fix retry: %d\n", err);
        return;
    }

    err = nrf_modem_gnss_fix_interval_set(1);
    if (err) {
        printk("Failed to set fix interval: %d\n", err);
        return;
    }

    /* 启动 GNSS 接收 (PVT) */
    err = nrf_modem_gnss_start();
    if (err) {
        printk("Failed to start GNSS: %d\n", err);
        return;
    }

    printk("=== GNSS started after LTE ready ===\n");
}

/* ====================== GNSS & 状态机定义 ====================== */

/* 当前 GNSS 状态（对外通过 message.status 告诉 LTE） */
static enum gnss_status current_status = GNSS_STATUS_SEARCHING;

/* 最近一次的原始 PVT 数据 */
static struct nrf_modem_gnss_pvt_data_frame last_pvt;

/* PVT 到达的信号量（在 GNSS 线程中处理） */
static K_SEM_DEFINE(pvt_data_sem, 0, 1);

/* 给 LTE 任务的消息队列定义 */
K_MSGQ_DEFINE(gnss_msgq, sizeof(struct gnss_status_msg), 16, 4);

/* 简化后的“当前 GNSS fix”结构（内部用） */
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

/* 喝水统计：内部仍然用 ms 累计，发给外部用秒 */
struct water_visit_stats {
    int64_t total_duration_ms;
};

static struct water_visit_stats water_stats = { 0 };

/* 丢星检测：记录连续 no-fix 次数 & 是否处于 GNSS lost 状态 */
static int  no_fix_count = 0;
static bool gnss_lost    = false;

/* 标记：是否需要在下一次有有效 fix 时，发送一条 is_water_gnss = true 的 message */
static bool trough_msg_pending = false;

/* ====================== LED 控制 ====================== */

enum gps_led_color {
    GPS_LED_OFF = 0,
    GPS_LED_RED,
    GPS_LED_YELLOW,
    GPS_LED_GREEN,
};

static void gps_led_set(enum gps_led_color color)
{
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

/* 把 GNSS UTC 小时转成“费城时间小时”（简单版：只做 hour + offset 的 0~23 wrap） */
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

/* ====================== 喝水逻辑 ====================== */

/* water_zone_update:
 *   根据当前位置更新 in_zone 状态，如果本次调用导致“离开水槽区域并且停留时间足够长”，
 *   则返回 true，并通过 out_duration_ms 返回停留时间。
 */
static bool water_zone_update(const struct gnss_fix_simple *fix,
                              int64_t *out_duration_ms)
{
    if (!trough_pos.valid) {
        return false;
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
            if (out_duration_ms) {
                *out_duration_ms = duration_ms;
            }
            return true; /* 产生了一次喝水事件 */
        }
    }

    return false;
}

/* 处理喝水事件：累计总时间 + 打 log */
static void handle_water_visit(const struct gnss_fix_simple *start_fix,
                               int64_t duration_ms)
{
    water_stats.total_duration_ms += duration_ms;

    uint16_t local_hour = utc_hour_to_philly(start_fix->hour);

    LOG_INF("WATER VISIT: duration = %lld ms, total = %lld ms, "
            "pos = (%f, %f), local time = %04u-%02u-%02u %02u:%02u:%02u.%03u (Philly)",
            (long long)duration_ms,
            (long long)water_stats.total_duration_ms,
            (double)start_fix->lat, (double)start_fix->lon,
            start_fix->year, start_fix->month, start_fix->day,
            local_hour, start_fix->minute, start_fix->seconds, start_fix->ms);
}

/* ====================== 按键：用于标记水槽位置 ====================== */

static void on_button1_pressed(void)
{
    /* 只在有 fix 且还没记水槽位置时处理：
     * 状态: WAIT_TROUGH_MARK, 并且 latest_fix.valid == true.
     */
    if (current_status != GNSS_STATUS_WAIT_TROUGH_MARK) {
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

    current_status = GNSS_STATUS_NORMAL;
    gps_led_set(GPS_LED_GREEN);

    /* 标记：下一次有有效 fix 时，发一条 is_water_gnss = true 的 message */
    trough_msg_pending = true;

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

/* ====================== 对 LTE 任务发 message ====================== */

/* 把当前内部状态 + 标志，组织成一条 message 丢给 LTE 任务
 * 注意：latest_fix 可能是“最后一次成功 fix”的值，在 fix_valid==false 时不会更新。
 */
static void send_gnss_message(bool is_water_gnss)
{
    struct gnss_status_msg msg = { 0 };

    msg.lat = latest_fix.lat;
    msg.lon = latest_fix.lon;
    msg.alt = latest_fix.alt;

    msg.speed_mps   = latest_fix.speed_mps;
    msg.heading_deg = latest_fix.heading_deg;

    msg.total_water_s = (double)water_stats.total_duration_ms / 1000.0;

    msg.is_water_gnss = is_water_gnss;
    msg.status        = current_status;

    /* 关键修改：
     * 队列如果满了，先清空旧数据，再放入当前这条最新状态，
     * 确保 GNSS 一旦拿到 valid fix，最新的经纬度不会被旧的 0,0 挡在外面。
     */
    int err = k_msgq_put(&gnss_msgq, &msg, K_NO_WAIT);
    if (err != 0) {
        /* 队列满：清空所有旧消息，只保留最新这一条 */
        k_msgq_purge(&gnss_msgq);
        (void)k_msgq_put(&gnss_msgq, &msg, K_NO_WAIT);
    }
}

/* ====================== PVT 处理（由 GNSS 线程调用） ====================== */

static void handle_pvt(const struct nrf_modem_gnss_pvt_data_frame *pvt)
{
    bool fix_valid = (pvt->flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID);
    bool is_water_gnss = false;

    if (!fix_valid) {
        /* 没有 fix 的情况 */

        if (current_status == GNSS_STATUS_NORMAL && trough_pos.valid) {
            no_fix_count++;

            /* 正常 -> 丢星：达到阈值 */
            if (!gnss_lost && no_fix_count >= NO_FIX_THRESHOLD) {
                gnss_lost = true;
                current_status = GNSS_STATUS_SIGNAL_LOST;
                gps_led_set(GPS_LED_RED);
                gnss_restart();

                LOG_INF("GNSS considered lost (no fix %d times)", no_fix_count);
            }
        }

        /* 如果之前已经 gnss_lost，就保持 SIGNAL_LOST 状态；
         * 如果还是 SEARCHING 或 WAIT_TROUGH_MARK，就保持原状态。
         * latest_fix 不更新，继续使用上一次成功 fix 的值。
         */

    } else {
        /* fix_valid == true */
        no_fix_count = 0;

        /* 如果之前处于 gnss_lost 状态，现在算是恢复了 */
        if (gnss_lost) {
            gnss_lost = false;

            if (trough_pos.valid) {
                current_status = GNSS_STATUS_NORMAL;
                gps_led_set(GPS_LED_GREEN);
            } else {
                /* 理论上很少发生：lost 时我们是在 NORMAL 且有 trough */
                LOG_WRN("GNSS fix restored but trough_pos invalid?");
                current_status = GNSS_STATUS_WAIT_TROUGH_MARK;
                gps_led_set(GPS_LED_YELLOW);
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
        if (current_status == GNSS_STATUS_SEARCHING) {
            current_status = GNSS_STATUS_WAIT_TROUGH_MARK;
            gps_led_set(GPS_LED_YELLOW);

            LOG_INF("Got first valid GNSS fix, waiting for trough mark (Button1)");
        }

        /* 喝水逻辑（只在 NORMAL 且已设水槽时） */
        if (current_status == GNSS_STATUS_NORMAL && trough_pos.valid) {
            int64_t visit_duration_ms = 0;
            bool new_visit = water_zone_update(&latest_fix, &visit_duration_ms);

            if (new_visit) {
                handle_water_visit(&water_state.enter_fix, visit_duration_ms);
            }
        }

        /* 是否是“水槽位置 GNSS”这一帧 */
        if (trough_msg_pending) {
            is_water_gnss = true;
            trough_msg_pending = false;
        }
    }

    /* 不管这次 PVT fix_valid 与否，都发一条 message：
     * - fix_valid == true：latest_fix 刚更新，可能有新水数据 / 新水槽位置；
     * - fix_valid == false：latest_fix 保持之前成功 fix 的值；
     *   上层看到 status = SEARCHING / SIGNAL_LOST 等，就知道此时坐标不一定可信。
     */
    send_gnss_message(is_water_gnss);
}

/* ====================== GNSS 事件回调（中断上下文） ====================== */

static void gnss_event_handler(int event)
{
    if (event == NRF_MODEM_GNSS_EVT_PVT) {
        if (nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt),
                                NRF_MODEM_GNSS_DATA_PVT) == 0) {
            k_sem_give(&pvt_data_sem);
        }
    }
    /* 如果以后需要 NMEA，可以在这里加 NRF_MODEM_GNSS_EVT_NMEA 处理 */
}

/* ====================== GNSS 初始化 ====================== */

/*
 * 注意：现在 gnss_init_and_start 只做 “注册 handler”，不真正 start GNSS，
 * 也不在这里设置 fix_retry/fix_interval。
 * 真正的配置 + start 由 gnss_start_after_lte_ready() 在 LTE L4_CONNECTED 之后调用。
 */
static int gnss_init_and_start(void)
{
    int err;

    /* 设置 GNSS 事件回调 */
    err = nrf_modem_gnss_event_handler_set(gnss_event_handler);
    if (err) {
        LOG_ERR("Failed to set GNSS event handler, err %d", err);
        return err;
    }

    LOG_INF("GNSS init done (event handler set). Waiting for LTE to start GNSS.");
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

    /* 开机默认红灯（还在搜星 / 系统初始化阶段） */
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

/* ====================== GNSS 线程函数 ====================== */

static void gnss_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("GNSS task thread started");

    while (1) {
        /* 等待新的 PVT 数据 */
        k_sem_take(&pvt_data_sem, K_FOREVER);
        handle_pvt(&last_pvt);
    }
}

K_THREAD_DEFINE(gnss_thread_id,
                GNSS_TASK_STACK_SIZE,
                gnss_thread,
                NULL, NULL, NULL,
                GNSS_TASK_PRIORITY, 0, 0);

/* ====================== 对外初始化接口 ====================== */

int gnss_system_init(void)
{
    int err;

    err = hardware_init();
    if (err) {
        LOG_ERR("hardware_init failed, err %d", err);
        return err;
    }

    /* 这里只做 GNSS 配置（注册 handler），不真正 start，也不改 retry/interval。 */
    err = gnss_init_and_start();
    if (err) {
        LOG_ERR("gnss_init_and_start failed, err %d", err);
        return err;
    }

    LOG_INF("GNSS system initialized (handler set, waiting for LTE to start GNSS)");
    return 0;
}
