#ifndef GNSS_TASK_H_
#define GNSS_TASK_H_

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

/* 对外暴露的 GNSS 状态，用于 LTE 任务判断情况 */
enum gnss_status {
    GNSS_STATUS_SEARCHING = 0,   /* 还在搜星 */
    GNSS_STATUS_WAIT_TROUGH_MARK,/* 已有 fix，等待用户按键标记水槽位置 */
    GNSS_STATUS_NORMAL,          /* 正常跟踪 + 已经有水槽位置 */
    GNSS_STATUS_SIGNAL_LOST,     /* 判断为 GNSS 信号丢失（连续多次无 fix） */
};

/* 给 LTE 任务发的消息结构体 */
struct gnss_status_msg {
    /* 位置 */
    double lat;
    double lon;
    double alt;

    /* 运动信息 */
    float  speed_mps;     /* 水平速度 (m/s) */
    float  heading_deg;   /* 航向角 (deg) */

    /* 喝水累计时间（秒） */
    double total_water_s;

    /* 标志：这一帧是否专门表示“水槽位置” */
    bool   is_water_gnss;

    /* 当前 GNSS 状态（搜索 / 等待标记 / 正常 / 丢星） */
    enum gnss_status status;
};

/* GNSS 任务对外导出的消息队列（LTE 任务从这里收消息） */
extern struct k_msgq gnss_msgq;

/* 初始化 GNSS 子系统（硬件 + GNSS 参数），不真正 start GNSS。
 * 真正 start 放在 gnss_start_after_lte_ready() 里做。
 */
int gnss_system_init(void);

/* 在 LTE L4_CONNECTED 之后调用，真正启用 GNSS 功能模式并 start GNSS */
void gnss_start_after_lte_ready(void);

#endif /* GNSS_TASK_H_ */
