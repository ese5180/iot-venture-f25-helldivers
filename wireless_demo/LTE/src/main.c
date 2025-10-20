#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
    printk("printk test via RTT\n");
    LOG_INF("LOG_INF test via RTT");
    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
