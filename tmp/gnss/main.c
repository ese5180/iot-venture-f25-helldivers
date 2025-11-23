/* main.c
 *
 * Example "LTE task":
 * - initializes the modem,
 * - starts the GNSS task system,
 * - then receives gnss_status_msg from gnss_msgq and logs them.
 *
 * 在真实项目里，你可以把这个 while(1) 换成：
 *   - 解析 msg，
 *   - 打包 JSON / 二进制，
 *   - 用 LTE-M 上传到云端。
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main_app, LOG_LEVEL_INF);

#include <modem/nrf_modem_lib.h>

#include "gnss_task.h"

int main(void)
{
    int err;

    LOG_INF("Main(LTE) app starting...");

    err = nrf_modem_lib_init();
    if (err) {
        LOG_ERR("nrf_modem_lib_init failed, err %d", err);
        return err;
    }

    err = gnss_system_init();
    if (err) {
        LOG_ERR("gnss_system_init failed, err %d", err);
        return err;
    }

    LOG_INF("Main(LTE): GNSS task initialized, now receiving GNSS messages");

    while (1) {
        struct gnss_status_msg msg;

        /* 阻塞等待 GNSS task 的消息 */
        err = k_msgq_get(&gnss_msgq, &msg, K_FOREVER);
        if (err) {
            continue;
        }

        const char *status_str = "UNKNOWN";

        switch (msg.status) {
        case GNSS_STATUS_SEARCHING:
            status_str = "SEARCHING";
            break;
        case GNSS_STATUS_WAIT_TROUGH_MARK:
            status_str = "WAIT_TROUGH_MARK";
            break;
        case GNSS_STATUS_NORMAL:
            status_str = "NORMAL";
            break;
        case GNSS_STATUS_SIGNAL_LOST:
            status_str = "SIGNAL_LOST";
            break;
        default:
            break;
        }

        LOG_INF("GNSS MSG: status=%s, lat=%.6f, lon=%.6f, alt=%.2f, "
                "speed=%.2f m/s, heading=%.1f deg, "
                "is_water_gnss=%d, total_water_s=%.3f",
                status_str,
                msg.lat, msg.lon, msg.alt,
                msg.speed_mps, msg.heading_deg,
                msg.is_water_gnss ? 1 : 0,
                msg.total_water_s);

        /* TODO: 在这里把 msg 打包发给 LTE-M 上传云端 */
    }

    return 0;
}
