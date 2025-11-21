/* gnss_task.h
 *
 * GNSS + water-intake task interface.
 * GNSS task runs as a Zephyr thread, and sends status messages
 * (position, speed, heading, water-time, flags) to a msgq.
 */

#ifndef GNSS_TASK_H_
#define GNSS_TASK_H_

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

/* GNSS 状态枚举，给 LTE 任务看 */
enum gnss_status {
    GNSS_STATUS_SEARCHING = 0,      /* 还在搜星 / 没有 fix */
    GNSS_STATUS_WAIT_TROUGH_MARK,   /* 有 fix，等待用户按键标记水槽 */
    GNSS_STATUS_NORMAL,             /* 已标记水槽，正常工作、正常 fix */
    GNSS_STATUS_SIGNAL_LOST,        /* 之前正常，现在连续多次没 fix，认为丢星 */
};

/* GNSS 任务发给 LTE 任务的 message */
struct gnss_status_msg {
    double lat;
    double lon;
    double alt;

    float  speed_mps;        /* 水平速度 m/s */
    float  heading_deg;      /* 运动方向 deg */

    double total_water_s;    /* 累计喝水停留时间（秒，带小数） */

    bool   is_water_gnss;    /* true: 这条是“水槽位置 GNSS”；false: 马的位置 */
    enum gnss_status status; /* 当前 GNSS 状态 */
};

/* 供 LTE 任务读取消息的队列
 * 使用方法：
 *   struct gnss_status_msg msg;
 *   k_msgq_get(&gnss_msgq, &msg, K_FOREVER);
 */
extern struct k_msgq gnss_msgq;

/* 由 main(LTE) 调用，用于初始化硬件（LED/按键）和 GNSS，并启动 GNSS 工作 */
int gnss_system_init(void);

#endif /* GNSS_TASK_H_ */
