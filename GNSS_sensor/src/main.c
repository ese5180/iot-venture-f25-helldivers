#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>

#include "gnss_task.h"
#include "sensor_task.h"

LOG_MODULE_REGISTER(main_app, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("=== SYSTEM START ===");

    nrf_modem_lib_init();
    gnss_system_init();
    sensor_task_start();

    while (1) {
        struct gnss_status_msg gnss_msg;
        sensor_data_msg_t sensor_msg;

        k_msgq_get(&gnss_msgq, &gnss_msg, K_FOREVER);
        sensor_task_get_data(&sensor_msg);

        LOG_INF("GPS: %.6f %.6f", gnss_msg.lat, gnss_msg.lon);

        LOG_INF("IMU: H=%.2f R=%.2f P=%.2f",
                sensor_msg.bno_heading,
                sensor_msg.bno_roll,
                sensor_msg.bno_pitch);

        LOG_INF("TEMP: %.2f HUM=%.2f",
                sensor_msg.temperature,
                sensor_msg.humidity);
    }
}
