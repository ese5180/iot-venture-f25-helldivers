/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gnss_sample, LOG_LEVEL_INF);

#include <stdio.h>
#include <nrf_modem_gnss.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

/* 最近一次的 PVT 数据 */
static struct nrf_modem_gnss_pvt_data_frame last_pvt;

/* NMEA 消息队列（和官方用法一致，但我们只打印，不做复杂转发） */
K_MSGQ_DEFINE(nmea_queue, sizeof(struct nrf_modem_gnss_nmea_data_frame *), 8, 4);

/* 用于 PVT 到达时的通知（保持与示例类似的事件/轮询结构） */
static K_SEM_DEFINE(pvt_data_sem, 0, 1);

/* poll 等待的两个事件：PVT 信号量 + NMEA 队列 */
static struct k_poll_event events[2] = {
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                    K_POLL_MODE_NOTIFY_ONLY,
                                    &pvt_data_sem, 0),
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                                    K_POLL_MODE_NOTIFY_ONLY,
                                    &nmea_queue, 0),
};

/* 打印 Tracking / Using / Unhealthy（沿用官方 PVT.sv[] 统计方式） */
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

/* GNSS 事件回调：只处理 PVT 与 NMEA，其他统统移除 */
static void gnss_event_handler(int event)
{
    if (event == NRF_MODEM_GNSS_EVT_PVT) {
        if (nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt), NRF_MODEM_GNSS_DATA_PVT) == 0) {
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
            /* 直接放队列，主循环里打印并释放 */
            if (k_msgq_put(&nmea_queue, &nmea, K_NO_WAIT) != 0) {
                k_free(nmea);
            }
        } else {
            k_free(nmea);
        }
    }
}

/* 仅做 GNSS 必要配置并启动（不连网、不辅助） */
static int gnss_init_and_start(void)
{
    if (lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS) != 0) {
        LOG_ERR("Failed to activate GNSS functional mode");
        return -1;
    }

    if (nrf_modem_gnss_event_handler_set(gnss_event_handler) != 0) {
        LOG_ERR("Failed to set GNSS event handler");
        return -1;
    }

    /* 只开启 GGA + GLL 两类 NMEA */
    uint16_t nmea_mask = NRF_MODEM_GNSS_NMEA_GGA_MASK |
                         NRF_MODEM_GNSS_NMEA_GLL_MASK;
    if (nrf_modem_gnss_nmea_mask_set(nmea_mask) != 0) {
        LOG_ERR("Failed to set NMEA mask");
        return -1;
    }

    /* 连续跟踪：每秒一帧；0 表示一直搜 */
    if (nrf_modem_gnss_fix_retry_set(0) != 0) {
        LOG_ERR("Failed to set fix retry");
        return -1;
    }
    if (nrf_modem_gnss_fix_interval_set(1) != 0) {
        LOG_ERR("Failed to set fix interval");
        return -1;
    }

    if (nrf_modem_gnss_start() != 0) {
        LOG_ERR("Failed to start GNSS");
        return -1;
    }

    return 0;
}

int main(void)
{
    LOG_INF("Minimal GNSS sample: stats + GGA/GLL only");

    if (nrf_modem_lib_init() != 0) {
        LOG_ERR("nrf_modem_lib_init failed");
        return -1;
    }

    if (gnss_init_and_start() != 0) {
        return -1;
    }

    while (1) {
        (void)k_poll(events, 2, K_FOREVER);

        /* 有新的 PVT，打印三项统计 */
        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE &&
            k_sem_take(events[0].sem, K_NO_WAIT) == 0) {
            print_satellite_stats(&last_pvt);
        }

        /* 有新的 NMEA，透传（只有 GGA/GLL 会出现） */
        if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
            struct nrf_modem_gnss_nmea_data_frame *nmea;
            while (k_msgq_get(&nmea_queue, &nmea, K_NO_WAIT) == 0) {
                printf("%s", nmea->nmea_str);
                k_free(nmea);
            }
        }

        events[0].state = K_POLL_STATE_NOT_READY;
        events[1].state = K_POLL_STATE_NOT_READY;
    }
}
