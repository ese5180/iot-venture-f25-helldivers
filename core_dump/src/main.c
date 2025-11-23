#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

LOG_MODULE_REGISTER(core_dump, LOG_LEVEL_INF);

/* 使用板子的 led0 alias */
#define LED0_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 alias is not defined in devicetree"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* 初始化 modem + 同步连接 LTE-M */
static void modem_init_and_connect(void)
{
    int err;

    LOG_INF("Initializing nRF modem library");

    err = nrf_modem_lib_init();
    if (err < 0) {
        LOG_ERR("nrf_modem_lib_init() failed, err %d", err);
        return;
    }

    LOG_INF("Connecting LTE (blocking)...");
    /* 在新版本 NCS 里，用 lte_lc_connect() 代替 lte_lc_init_and_connect() */
    err = lte_lc_connect();
    if (err) {
        LOG_ERR("lte_lc_connect() failed, err %d", err);
    } else {
        LOG_INF("LTE connected");
    }
}

void main(void)
{
    int ret;

    LOG_INF("core_dump blinky with LTE start");

    if (!device_is_ready(led0.port)) {
        LOG_ERR("LED device not ready");
        return;
    }

    ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        LOG_ERR("Failed to configure LED pin, err %d", ret);
        return;
    }

    /* 初始化 modem + 连接 LTE-M */
    modem_init_and_connect();

    /* 
     * 如果你想测试 Memfault coredump，可以在下面加一个“自杀”故障，例如：
     *
     *   k_sleep(K_SECONDS(30));
     *   volatile int *bad = (int *)0x0;
     *   *bad = 42;   // 故意造成 HardFault，Memfault 会抓 core dump
     *
     * 先确认能正常连接、LED 闪，再自己按需要打开。
     */

    while (1) {
        gpio_pin_toggle_dt(&led0);
        k_sleep(K_SECONDS(1));
    }
}
