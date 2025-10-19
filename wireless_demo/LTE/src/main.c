// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

// /* 说明：
//  * CONFIG_AT_HOST_LIBRARY=y 后，at_host 会通过 SYS_INIT 自动初始化，
//  * 应用里不需要 include <modem/at_host.h> 或显式调用初始化函数。
//  */

// int main(void)
// {
//     LOG_INF("at_client passthrough ready (no auto attach)");
//     while (1) {
//         k_sleep(K_SECONDS(1));
//     }
//     return 0;
// }
#include <zephyr.h>
#include <zephyr/net/socket.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(at_client_offline, LOG_LEVEL_INF);

#define SERVER_ADDR "20.55.16.155"   //  IP
#define SERVER_PORT 1880             

void main(void)
{
    int err;
    LOG_INF("Starting LTE Hello World demo...");

    // Initialize modem
    err = nrf_modem_lib_init();
    if (err) {
        LOG_ERR("Modem init failed: %d", err);
        return;
    }

    // Connect to LTE
    LOG_INF("Connecting to LTE network...");
    err = lte_lc_init_and_connect();
    if (err) {
        LOG_ERR("LTE connect failed: %d", err);
        return;
    }

    LOG_INF("LTE connected successfully!");

    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        LOG_ERR("Socket creation failed");
        return;
    }

    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
    };
    inet_pton(AF_INET, SERVER_ADDR, &server.sin_addr);

    // Connect to server
    LOG_INF("Connecting to host...");
    err = connect(sock, (struct sockaddr *)&server, sizeof(server));
    if (err < 0) {
        LOG_ERR("Connect failed, errno: %d", errno);
        close(sock);
        return;
    }

    // Send message
    const char msg[] = "Hello World from nRF9151!\n";
    send(sock, msg, strlen(msg), 0);
    LOG_INF("Message sent!");

    close(sock);
    LOG_INF("Done.");
}