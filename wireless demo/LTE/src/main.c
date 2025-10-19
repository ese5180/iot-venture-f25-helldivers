#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* 说明：
 * CONFIG_AT_HOST_LIBRARY=y 后，at_host 会通过 SYS_INIT 自动初始化，
 * 应用里不需要 include <modem/at_host.h> 或显式调用初始化函数。
 */

int main(void)
{
    LOG_INF("at_client passthrough ready (no auto attach)");
    while (1) {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
