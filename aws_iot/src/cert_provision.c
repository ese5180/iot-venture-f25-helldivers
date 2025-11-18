#include <zephyr/logging/log.h>
#include <modem/modem_key_mgmt.h>
#include <string.h>

LOG_MODULE_REGISTER(cert_provision, CONFIG_LOG_DEFAULT_LEVEL);

/* Must match CONFIG_MQTT_HELPER_SEC_TAG in prj.conf */
#define SEC_TAG 201

/* AWS Root CA (AmazonRootCA1.pem) */
static const char aws_root_ca_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
"IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
"VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
"93FcXmn/qoQj8WDtxJiqZ6m9FFZoPBzH+YF2dUyCBj4N3utoFWvAiJZPmnfntr6i\n"
"R8+N+3G3kpkCAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
"U5PMCCjjmCx7ycQeFZ4s+pHoO0etp6bI/+FFkCGCzBPC4mPdFiAceBCLgLnu3bkq\n"
"jB0Tt13ZTQWeK6yjM/rb+UZU7dY8LGtFZ7l4I8Mbcn8i5BZm0laUfqR61NrvU+1h\n"
"INHY5GHA3ny3gjxYa3kBs7qNgBi7qKPvXxLVv9o/ICgOo3T9qs/5cY5knld0yHtA\n"
"yxB3T7zINBuCvdT2Vwh3i3TJE6r1pisGLFyy0Awamu7eV/qjpqVWh3D48KMFzXRI\n"
"2zGh6Ycx/j6K+tD8qjdkSOnC3FIq\n"
"-----END CERTIFICATE-----\n";

/* Device certificate (…certificate.pem.crt) */
static const char client_cert_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDWTCCAkGgAwIBAgIUcNVSFNS2LuIrD7eo0rVpSgEljpYwDQYJKoZIhvcNAQEL\n"
"BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n"
"SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MTExMTA0MTI0\n"
"MVoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n"
"ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAK+63da/k5PhOdEI6udp\n"
"wKaNlYDVogNodSz/P46whUgSg/wsC1zgCo+5OZK7u22yk1ycPT495Qjm7mvo+e5+\n"
"Cwhyrc8yzNq1pGsrnDBq51pKruko8Dd7QH4qZyo7uo5MP2OwkzDvQHOz9/+clSQK\n"
"Oi3yCaE7H6dEtw8TADrFoY4eHryGPxJfEuphJIdnFTfCPccXl/FuiI+kCaZ2isX3\n"
"kK6blL+dQoGZWEmbVb91T1slSiUBKB0OeqouOdT0PWD3KIoy6/Ml6IvAUyLVyflp\n"
"YfVStSs7+TJocznoHn2jKjSn6X3JM3Ztnt0yhu7LT5YotR1NH+jLmfG/zGsoPE81\n"
"/jsCAwEAAaNgMF4wHwYDVR0jBBgwFoAU5+GAl4xSo9meHLsFp6bCTdZYPgQwHQYD\n"
"VR0OBBYEFFAENZbslGi5go8gnnEODAqFS6WNMAwGA1UdEwEB/wQCMAAwDgYDVR0P\n"
"AQH/BAQDAgWgMA0GCSqGSIb3DQEBCwUAA4IBAQBJARnjeOXdlGafu/ceE3CvhnUL\n"
"QlUL/pmVKZG3w3JxSHzxWsejzzTpEzTAtEAoqXhz+Yj9/PrCrlzckQmowdP7aIXk\n"
"2O3X4C3DGBsCoU9QO6GUTsjC7bGk7MyO9jnXfhetf6zWV8M9nu+exwG1d3cN26a5\n"
"I2OCQstjEqN7v7tIPnF5kRAXSgXPZsbz3es2mHE//khoyiQ9k0BwtigMkjg/5YwU\n"
"1YFKCJjFA+sbbhyaKaDpXozYdOtbKHLtHWJQbfC3e6xMqJrSBbVOEYVCu4HZzCKD\n"
"rLySI75JhIsirS3MOFqYlaKuUkZ4zmJPm3MiXHeIW74a9iZptTQ6pqyxq8gf\n"
"-----END CERTIFICATE-----\n";

/* Device private key (…private.pem.key) */
static const char client_key_pem[] =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEogIBAAKCAQEAr7rd1r+Tk+E50Qjq52vApo2VgNWgDaHUs/z+OsIVIMoP8LXO\n"
"AKj7k5kru7tspNcnD0+PeUI5u5r6PnufgsIcq3/MszataRrK5wwaudaSq7pK/A3e\n"
"0B+KmcqG7qOOTD9jsJMA70Bzs/f/nJUkCjot8gmhOx+nRLcPEwA6xaGOHh68hj8S\n"
"XxLqYCSXZxU3wj3HF5fxboiPpAmmdorF95Cum5S/nUKBmVhLm1W/dU9bJUolASgd\n"
"DnqqLjnU9D1gtyiKMuvzJeiLwFMi1cn5qWH1UrNLO/kyaHM56B0WYXk7qs0RIhbG\n"
"xAdqJItqC2E21C66p9cO158bbZthXXMf6fCgkQIDAQABAoIBAAfkamQBjWE0oa2Q\n"
"JE+H5CkSLJQpWucG2IRF5FzbPsrOMq3TkpZBTptDXVRr2rn/lgLyyK3XKXgWkMgP\n"
"ePl3Rx1aIIPVkRtpYTDgun/tS5kCDDfm3bflSUaP7BtCyY0jIp4Rkd5gElwegcNq\n"
"KZMTxcTt4Jx7vmSAvGXAXWZXjnwHuaWDhrMNWMp401KsdJ7aGLjiMCawQP8MQJ7y\n"
"lUko4VyXR+Ob2BvqbYehaq212QBzzyLjEbLoB5Tkf9zsgtR02LVB37N8+C2dzVJx\n"
"m+OdWSowxZqG2a7odlUTmXnhI3/M1Ryk1hXAOUvZSu7i5mZV2jzRTZFF27ecxGle\n"
"yVVsxGkCgYEA1Z1TBQKrXWO/1bpjuo5D5/kVfn/ZyMP+Ik6Yp8LWXO/LIA5fVzoY\n"
"40Ec5QtFV0RjI1fQsj83qNAhtu8w+llBzt4xspQkHpau+l7lV4bGG4nZd8Nh8pJ/\n"
"ctW/Y7VLSI0/wXpfiCv6skLjGtXocA+bzM5Rf7MXG/xxIXQYsdoq9Z0CgYEAw1pO\n"
"9YBRhKaY3dec01FG/B5DAWPStinUizvVJ01lXdXAuyGb91hpnLd8aj5P1kLVYEp+\n"
"hQj71BtzdALmWACFZ6dHcof445Siu7bJKz1YVu41VkXB+dql5SgmoCRTE4zD13bV\n"
"P+Y5eRKOoafcq2yWn0+pKA9b45yjaER46DdthJkCgYBu5Nm5Qc4UVkX/QE3QzvdY\n"
"OUtdBpXUIuoFQifgjWeeRJWqrw6LLEgfl4oA2Jdqmqm9iczKh5obkXYDDmFFHUlS\n"
"ALSR/ttiyH+tUtC1OO+TKCbNaUDGBzugnpV6Mozl4Wg7bjSC1z7wgoYbqSnOfBe7\n"
"DeIC5ZLIUZMYmneViJ20PQKBgDRuJycY83HMEczvOsVPPVGCZMjf9nOyVFYA2zM0\n"
"7QcuzxqpQK1q7QHBpc0YEeSLUIQEcM6RYDxs25aNwXvejBY+yf+cZiXAmqksXpoL\n"
"Kl1H1QlDfmg1Q+62vFQtkPFQcZQLmWRlku9HbjNho2VnnUYQLH2TQTr/cvQFdb1v\n"
"BLvjAoGAPthOUtMFkhm6GmSkmzye0VW7FvEBmMSeCYOP820WQw8/s9yJ5Z+1pEfh\n"
"93HK2j11O9ljyYYgiL1kKrybmPAFYdLRi4+ngturlQmPUejdtlBer0HSdXOCcO26\n"
"tq9Oym1v3LBrJBLnFMYULh/4ZG9O6N7E4I3+b5khBslH36r+TI0=\n"
"-----END RSA PRIVATE KEY-----\n";

static int write_cred(int tag, enum modem_key_mgmt_cred_type type, const char *src)
{
    int err = modem_key_mgmt_write(tag, type, src, strlen(src));
    if (err) {
        LOG_ERR("modem_key_mgmt_write(tag=%d, type=%d) failed: %d", tag, type, err);
        return err;
    }
    LOG_INF("Stored credential type %d in sec tag %d", type, tag);
    return 0;
}

int provision_credentials(void)
{
    int err;

    LOG_INF("Provisioning credentials into sec tag %d", SEC_TAG);

    /* 调试阶段：每次启动都先删掉旧的，再写新的 */
    (void)modem_key_mgmt_delete(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
    (void)modem_key_mgmt_delete(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT);
    (void)modem_key_mgmt_delete(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT);

    err = write_cred(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, aws_root_ca_pem);
    if (err) {
        return err;
    }

    err = write_cred(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT, client_cert_pem);
    if (err) {
        return err;
    }

    err = write_cred(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT, client_key_pem);
    if (err) {
        return err;
    }

    LOG_INF("Credential provisioning finished OK");
    return 0;
}
