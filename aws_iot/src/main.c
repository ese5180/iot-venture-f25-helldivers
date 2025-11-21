#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include <stdlib.h>
#include <hw_id.h>
#include <modem/modem_info.h>

#include "json_payload.h"
#include "horse_payload.h"
#include <modem/nrf_modem_lib.h>
#include "gnss_task.h"
#include <modem/lte_lc.h>

/*===============================parameter==========================*/

/*===============================change code==========================*/
static void horse_data_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(horse_data_work, horse_data_work_fn);

/*==========================================hores_data=======================================*/
void publish_horse_data(float temperature, float moisture, float pitch,
                        float gps_lat, float gps_lon,
                        int water_flag, int water_time)
{
    char json_buf[256];
    struct horse_payload hp;

    /* new fields */
    hp.water_flag = water_flag;
    hp.water_time = water_time;

    /* existing fields */
    hp.temperature = (int32_t)(temperature * 100.0f);
    hp.moisture    = (int32_t)(moisture * 100.0f);
    hp.pitch       = (int32_t)(pitch * 100.0f);
    hp.latitude    = (int32_t)(gps_lat * 1000000.0f);
    hp.longitude   = (int32_t)(gps_lon * 1000000.0f);

    if (horse_payload_construct(json_buf, sizeof(json_buf), &hp)) {
        printk("horse_payload_construct failed\n");
        return;
    }

    struct aws_iot_data tx = { 0 };
    tx.qos       = MQTT_QOS_1_AT_LEAST_ONCE;
    tx.ptr       = json_buf;
    tx.len       = strlen(json_buf);
    tx.topic.str = "horse_data";
    tx.topic.len = strlen(tx.topic.str);

    int err = aws_iot_send(&tx);
    printk("horse_data sent: %s\n", json_buf);
}

/* ================= hores_data update work ================= */
static void horse_data_work_fn(struct k_work *work)
{
    struct gnss_status_msg msg;
    while (k_msgq_get(&gnss_msgq, &msg, K_NO_WAIT) == 0) {
        /* 循环到最后一条 msg */
    }
    static struct gnss_status_msg last_msg = {0};
    if (msg.lat != 0) {
        last_msg = msg;
    }
    publish_horse_data(
        23.5f,
        56.7f,
        12.8f,
        last_msg.lat,
        last_msg.lon,
        last_msg.is_water_gnss ? 1 : 0,
        (int)last_msg.total_water_s
    );

    /* 10 秒后再次执行 */
    k_work_reschedule(&horse_data_work, K_SECONDS(10));
}



LOG_MODULE_REGISTER(aws_iot_sample, CONFIG_AWS_IOT_SAMPLE_LOG_LEVEL);

#define L4_EVENT_MASK         (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define MODEM_FIRMWARE_VERSION_SIZE_MAX 50

#define MY_CUSTOM_TOPIC_1 "my-custom-topic/example"
#define MY_CUSTOM_TOPIC_2 "my-custom-topic/example_2"

#define FATAL_ERROR()                             \
    LOG_ERR("Fatal error! Rebooting the device.");\
    LOG_PANIC();                                  \
    IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

static char hw_id[HW_ID_LEN];

static void shadow_update_work_fn(struct k_work *work);
static void connect_work_fn(struct k_work *work);
static void aws_iot_event_handler(const struct aws_iot_evt *const evt);

static K_WORK_DELAYABLE_DEFINE(shadow_update_work, shadow_update_work_fn);
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

/* ================= Application topics ================= */

static int app_topics_subscribe(void)
{
    int err;
    static const struct mqtt_topic topic_list[] = {
        {
            .topic.utf8 = MY_CUSTOM_TOPIC_1,
            .topic.size = sizeof(MY_CUSTOM_TOPIC_1) - 1,
            .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        },
        {
            .topic.utf8 = MY_CUSTOM_TOPIC_2,
            .topic.size = sizeof(MY_CUSTOM_TOPIC_2) - 1,
            .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        },
        {
            .topic.utf8 = "horse_data",
            .topic.size = sizeof("horse_data") - 1,
            .qos = MQTT_QOS_1_AT_LEAST_ONCE,
        }
    };

    err = aws_iot_application_topics_set(topic_list, ARRAY_SIZE(topic_list));
    if (err) {
        LOG_ERR("aws_iot_application_topics_set, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    return 0;
}

/* ================= AWS IoT client init ================= */

static int aws_iot_client_init(void)
{
    int err;

    printk("=== aws_iot_client_init: start ===\n");

    err = aws_iot_init(aws_iot_event_handler);
    if (err) {
        LOG_ERR("AWS IoT library could not be initialized, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    printk("=== aws_iot_client_init: aws_iot_init OK ===\n");

    err = app_topics_subscribe();
    if (err) {
        LOG_ERR("Adding application specific topics failed, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    printk("=== aws_iot_client_init: topics subscribed OK ===\n");

    return 0;
}

/* ================= Shadow update work ================= */

static void shadow_update_work_fn(struct k_work *work)
{
    int err;
    char message[CONFIG_AWS_IOT_SAMPLE_JSON_MESSAGE_SIZE_MAX] = { 0 };
    struct payload payload = {
        .state.reported.uptime = k_uptime_get(),
        .state.reported.app_version = CONFIG_AWS_IOT_SAMPLE_APP_VERSION,
    };
    struct aws_iot_data tx_data = {
        .qos = MQTT_QOS_0_AT_MOST_ONCE,
        .topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
    };

    if (IS_ENABLED(CONFIG_MODEM_INFO)) {
        char modem_version_temp[MODEM_FIRMWARE_VERSION_SIZE_MAX];

        err = modem_info_get_fw_version(modem_version_temp,
                        ARRAY_SIZE(modem_version_temp));
        if (err) {
            LOG_ERR("modem_info_get_fw_version, error: %d", err);
            FATAL_ERROR();
            return;
        }

        payload.state.reported.modem_version = modem_version_temp;
    }

    err = json_payload_construct(message, sizeof(message), &payload);
    if (err) {
        LOG_ERR("json_payload_construct, error: %d", err);
        FATAL_ERROR();
        return;
    }

    tx_data.ptr = message;
    tx_data.len = strlen(message);

    LOG_INF("Publishing message: %s to AWS IoT shadow", message);

    err = aws_iot_send(&tx_data);
    if (err) {
        LOG_ERR("aws_iot_send, error: %d", err);
        FATAL_ERROR();
        return;
    }

    (void)k_work_reschedule(&shadow_update_work,
        K_SECONDS(CONFIG_AWS_IOT_SAMPLE_PUBLICATION_INTERVAL_SECONDS));
}

/* ================= Connect work ================= */

static void connect_work_fn(struct k_work *work)
{
    int err;
    const struct aws_iot_config config = {
        .client_id = hw_id,
    };

    LOG_INF("Connecting to AWS IoT (client_id: %s, host: %s, port: %d)",
        hw_id,
        CONFIG_AWS_IOT_BROKER_HOST_NAME,
#ifdef CONFIG_MQTT_HELPER_PORT
        CONFIG_MQTT_HELPER_PORT
#else
        -1
#endif
    );

    err = aws_iot_connect(&config);
    printk("=== connect_work_fn: aws_iot_connect() returned: %d ===\n", err);

    if (err == -EAGAIN) {
        LOG_INF("Connection attempt timed out, next retry in %d seconds",
            CONFIG_AWS_IOT_SAMPLE_CONNECTION_RETRY_TIMEOUT_SECONDS);

        (void)k_work_reschedule(&connect_work,
            K_SECONDS(CONFIG_AWS_IOT_SAMPLE_CONNECTION_RETRY_TIMEOUT_SECONDS));
    } else if (err) {
        LOG_ERR("aws_iot_connect, error: %d", err);
        FATAL_ERROR();
    }
}

/* ================= AWS IoT event handler ================= */

static void on_aws_iot_evt_connected(const struct aws_iot_evt *const evt)
{
    (void)k_work_cancel_delayable(&connect_work);

    if (evt->data.persistent_session) {
        LOG_WRN("Persistent session enabled, reusing subscriptions");
    }

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    boot_write_img_confirmed();
#endif
    

    (void)k_work_reschedule(&shadow_update_work, K_NO_WAIT);

    k_work_reschedule(&horse_data_work, K_SECONDS(10));
}

static void on_aws_iot_evt_disconnected(void)
{
    (void)k_work_cancel_delayable(&shadow_update_work);
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
    switch (evt->type) {
    case AWS_IOT_EVT_CONNECTING:
        LOG_INF("AWS_IOT_EVT_CONNECTING");
        break;
    case AWS_IOT_EVT_CONNECTED:
        LOG_INF("AWS_IOT_EVT_CONNECTED");
        on_aws_iot_evt_connected(evt);
        break;
    case AWS_IOT_EVT_DISCONNECTED:
        LOG_INF("AWS_IOT_EVT_DISCONNECTED");
        on_aws_iot_evt_disconnected();
        break;
    case AWS_IOT_EVT_DATA_RECEIVED:
        LOG_INF("AWS_IOT_EVT_DATA_RECEIVED");
        LOG_INF("Received: \"%.*s\" on \"%.*s\"",
            evt->data.msg.len, evt->data.msg.ptr,
            evt->data.msg.topic.len, evt->data.msg.topic.str);
        break;
    case AWS_IOT_EVT_PUBACK:
        LOG_INF("AWS_IOT_EVT_PUBACK id=%d", evt->data.message_id);
        break;
    case AWS_IOT_EVT_PINGRESP:
        LOG_INF("AWS_IOT_EVT_PINGRESP");
        break;
    case AWS_IOT_EVT_ERROR:
        LOG_INF("AWS_IOT_EVT_ERROR %d", evt->data.err);
        FATAL_ERROR();
        break;
    default:
        LOG_WRN("Unknown AWS IoT event %d", evt->type);
        break;
    }
}

/* ================= Network event handlers ================= */

static void l4_event_handler(struct net_mgmt_event_callback *cb,
                 uint32_t event,
                 struct net_if *iface)
{
    switch (event) {
    case NET_EVENT_L4_CONNECTED:
        LOG_INF("Network connectivity established");
        printk("=== L4_CONNECTED: scheduling connect_work ===\n");
        //add
        gnss_start_after_lte_ready();
        //
        (void)k_work_schedule(&connect_work, K_NO_WAIT);
        break;
    case NET_EVENT_L4_DISCONNECTED:
        LOG_INF("Network connectivity lost");
        break;
    default:
        return;
    }
}

static void connectivity_event_handler(struct net_mgmt_event_callback *cb,
                       uint32_t event,
                       struct net_if *iface)
{
    if (event == NET_EVENT_CONN_IF_FATAL_ERROR) {
        LOG_ERR("NET_EVENT_CONN_IF_FATAL_ERROR");
        FATAL_ERROR();
    }
}




/* ============================ main ============================ */

// int main(void)
// {   
//     int err;
//     LOG_INF("Main(LTE) app starting...");
//     err = nrf_modem_lib_init();
//     if (err) {
//         LOG_ERR("nrf_modem_lib_init failed, err %d", err);
//         return err;
//     }
//     err = gnss_system_init();
//     if (err) {
//         LOG_ERR("gnss_system_init failed: %d", err);
//     }

//     extern int provision_credentials(void);

//     LOG_INF("The AWS IoT sample started, version: %s",
//         CONFIG_AWS_IOT_SAMPLE_APP_VERSION);

//     printk("=== HELLDIVERS AWS MAIN, built at " __DATE__ " " __TIME__ " ===\n");

    
//     net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
//     net_mgmt_add_event_callback(&l4_cb);

//     net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler,
//                      CONN_LAYER_EVENT_MASK);
//     net_mgmt_add_event_callback(&conn_cb);

//     LOG_INF("Bringing network interface up and connecting");
//     printk("=== Before conn_mgr_all_if_up ===\n");

    
//     err = conn_mgr_all_if_up(true);
//     printk("=== conn_mgr_all_if_up() returned: %d ===\n", err);
//     if (err) {
//         LOG_ERR("conn_mgr_all_if_up error: %d", err);
//         FATAL_ERROR();
//         return err;
//     }

    
//     err = provision_credentials();
//     if (err) {
//         LOG_ERR("provision_credentials failed: %d", err);
//         FATAL_ERROR();
//         return err;
//     }

//     printk("=== Before conn_mgr_all_if_connect ===\n");
//     err = conn_mgr_all_if_connect(true);
//     printk("=== conn_mgr_all_if_connect() returned: %d ===\n", err);
//     if (err) {
//         LOG_ERR("conn_mgr_all_if_connect error: %d", err);
//         FATAL_ERROR();
//         return err;
//     }

// #if defined(CONFIG_AWS_IOT_SAMPLE_CLIENT_ID_USE_HW_ID)
//     printk("=== Before hw_id_get ===\n");
//     err = hw_id_get(hw_id, ARRAY_SIZE(hw_id));
//     printk("=== hw_id_get() returned: %d ===\n", err);
//     if (err) {
//         LOG_ERR("Failed to retrieve hardware ID, error: %d", err);
//         FATAL_ERROR();
//         return err;
//     }
//     LOG_INF("Hardware ID: %s", hw_id);
//     printk("=== Hardware ID: %s ===\n", hw_id);
// #else
//     snprintf(hw_id, sizeof(hw_id), "%s", CONFIG_AWS_IOT_CLIENT_ID_STATIC);
//     LOG_INF("Using static client ID as hw_id: %s", hw_id);
//     printk("=== Using static client ID as hw_id: %s ===\n", hw_id);
// #endif

//     printk("=== Before aws_iot_client_init ===\n");
//     err = aws_iot_client_init();
//     printk("=== aws_iot_client_init() returned: %d ===\n", err);
//     if (err) {
//         LOG_ERR("aws_iot_client_init error: %d", err);
//         FATAL_ERROR();
//         return err;
//     }

//     if (IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)) {
//         conn_mgr_mon_resend_status();
//     }

//     printk("=== main() finished, waiting for events ===\n");

//     return 0;
// }
int main(void)
{   
    int err;

    LOG_INF("Main(LTE) app starting...");
    
    /* --------------------------------------------------
     * Step 1: 初始化 Modem （但不启动 LTE）
     * -------------------------------------------------- */
    err = nrf_modem_lib_init();
    if (err) {
        LOG_ERR("nrf_modem_lib_init failed, err %d", err);
        return err;
    }

    /* --------------------------------------------------
     * Step 2: 写 AWS 证书 (必须在 LTE 连接前)
     * -------------------------------------------------- */
    extern int provision_credentials(void);
    err = provision_credentials();
    if (err) {
        LOG_ERR("provision_credentials failed: %d", err);
        FATAL_ERROR();
        return err;
    }

    /* --------------------------------------------------
     * Step 3: 初始化 GNSS Task （不激活 GNSS）
     *      只创建线程，但不 call lte_lc_func_mode_set()
     *      GNSS 的激活必须等 LTE 连接之后才可以
     * -------------------------------------------------- */
    err = gnss_system_init();   
    if (err) {
        LOG_ERR("gnss_system_init failed: %d", err);
        /* 不 FATAL_ERROR，因为 GNSS 可以延后激活 */
    }

    /* --------------------------------------------------
     * Step 4: 注册 LTE 链路事件 (AWS 会在 L4_CONNECTED 启动 GNSS)
     * -------------------------------------------------- */
    net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
    net_mgmt_add_event_callback(&l4_cb);

    net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler,
                                 CONN_LAYER_EVENT_MASK);
    net_mgmt_add_event_callback(&conn_cb);

    printk("=== HELLDIVERS AWS MAIN, built at " __DATE__ " " __TIME__ " ===\n");
    LOG_INF("Bringing network interface up and connecting");
    printk("=== Before conn_mgr_all_if_up ===\n");

    /* --------------------------------------------------
     * Step 5: 启动 LTE (模块上线)
     * -------------------------------------------------- */
    err = conn_mgr_all_if_up(true);
    printk("=== conn_mgr_all_if_up() returned: %d ===\n", err);
    if (err) {
        LOG_ERR("conn_mgr_all_if_up error: %d", err);
        FATAL_ERROR();
        return err;
    }

    printk("=== Before conn_mgr_all_if_connect ===\n");

    /* --------------------------------------------------
     * Step 6: 连接网络（RRC 注册）
     * 连接成功后会进入 L4_CONNECTED 
     * 在 L4_CONNECTED 的 handler 中启动 AWS + GNSS activation
     * -------------------------------------------------- */
    err = conn_mgr_all_if_connect(true);
    printk("=== conn_mgr_all_if_connect() returned: %d ===\n", err);
    if (err) {
        LOG_ERR("conn_mgr_all_if_connect error: %d", err);
        FATAL_ERROR();
        return err;
    }

    /* --------------------------------------------------
     * Step 7: 设置 AWS Client ID
     * -------------------------------------------------- */
#if defined(CONFIG_AWS_IOT_SAMPLE_CLIENT_ID_USE_HW_ID)
    err = hw_id_get(hw_id, ARRAY_SIZE(hw_id));
    printk("=== hw_id_get() returned: %d ===\n", err);
    if (err) {
        LOG_ERR("Failed to retrieve hardware ID, error: %d", err);
        FATAL_ERROR();
        return err;
    }
    LOG_INF("Hardware ID: %s", hw_id);
#else
    snprintf(hw_id, sizeof(hw_id), "%s", CONFIG_AWS_IOT_CLIENT_ID_STATIC);
    LOG_INF("Using static client ID as hw_id: %s", hw_id);
#endif

    printk("=== Before aws_iot_client_init ===\n");

    /* --------------------------------------------------
     * Step 8: AWS IoT 初始化（但不连接）
     * AWS 连接会在 l4_event_handler: L4_CONNECTED 中触发
     * -------------------------------------------------- */
    err = aws_iot_client_init();
    printk("=== aws_iot_client_init() returned: %d ===\n", err);
    if (err) {
        LOG_ERR("aws_iot_client_init error: %d", err);
        FATAL_ERROR();
        return err;
    }

    printk("=== main() finished, waiting for events ===\n");

    return 0;
}
