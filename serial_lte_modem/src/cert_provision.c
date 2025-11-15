#include <zephyr/kernel.h>
#include <string.h>
#include <modem/modem_key_mgmt.h>

#define SEC_TAG 201

static const char aws_root_ca_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
"ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
"b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
"MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
"b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
"ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
"9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
"IfClKzbwQ6rfZYLTh2H3fdBgMmu9nadeWzDHPgaDbbt9cPY2zEPGQmim0ZKAJE51\n"
"HwoTWlfRWlt4QeZyNqfL2kZzFBQ9/2c6sQU+FDJKZ/c2jC9K3or9L5rfuHs9mJh5\n"
"qnxg/j91dFGMpTns1+rhsDd+cYN3pw7mf0lRzYDj7B1bW89oL7tgjP7HZ3RNw+LL\n"
"YaDQoZ3zH6kCAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
"rqXRfboQnoZsG4q5WTP468SQvvG5\n"
"-----END CERTIFICATE-----\n";

static const char device_cert_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDWjCCAkKgAwIBAgIVAJcUQ8Vae4YqMKsLdE3YZ16zj9ElMA0GCSqGSIb3DQEB\n"
"CwUAMG0xCzAJBgNVBAYTAlVTMSIwIAYDVQQKDBlBbWF6b24gV2ViIFNlcnZpY2Vz\n"
"IExpbWl0ZWQxMDAuBgNVBAMMJ0FtYXpvbiBSb290IENBIDEtRWMyNTYgU0hBMjU2\n"
"IFRlc3QgQ0EwHhcNMjUxMTE1MTQwMzAxWhcNMjYxMTE1MTUwMzAxWjBEMQswCQYD\n"
"VQQGEwJVUzEiMCAGA1UECgwbQW1hem9uIFdlYiBTZXJ2aWNlcyBMaW1pdGVkMRow\n"
"GAYDVQQDDBFhd3MuaW90LW1hcmtldHBsYTAwggEiMA0GCSqGSIb3DQEBAQUAA4IB\n"
"DwAwggEKAoIBAQDGyN/ozTiaFPoF6jWG3FTp2v4aZXP9m6C34YJONZ66NEyhwr+a\n"
"ZJvezl2KS7iR5nFGEZOkOFzZ3TvDp5sEClOQI4gtowZ47HRRSUiR/FbcVSW1eW5/\n"
"W4VvbTReAD5/E3oCwsG590fTK/6Xztoi2FGB8zij7J/+T+TUk1Yb3g0pXGkmjwdT\n"
"/3PgFUtZVsOhVi8JuU7QqhWEGlG3QGFaerkd0mYZieQSWhE1Kw9BWFIvaygHPygu\n"
"Av7vwv+/n5GUA83wlcjqxGqwlZ6efAkC5GNP8yy0k1hbq9wCul9m2Nzw2d0BWXhO\n"
"ArfPgVXWGBbWS854Iu1JqIFBB6z+L/mJA9l1AgMBAAGjYDBeMB8GA1UdIwQYMBaA\n"
"FIXpKlYAhredtRk/U4e1rMHuLZoBMB0GA1UdDgQWBBRFZU5ICeWrdVLc5Tf49VWJ\n"
"5vgYxTAOBgNVHQ8BAf8EBAMCB4AwDAYDVR0TAQH/BAIwADANBgkqhkiG9w0BAQsF\n"
"AAOCAQEAVrC32izbJH5XQuhbcyUZ1srThgiHBd7fFx3dABYnmRUWbIVvjGQ5VX1V\n"
"xrTr7VxEs/G88QtRm8njRko+98n3GI5b1fv3MmcoebvsUfD9SIyuEKWB9+H2+tay\n"
"2vKEYeRaxOPcsl+imqAhat6xtQnj0obOYtC2tP6Rawsr2Zacd8S/0ZwePc8FveAz\n"
"ze+Z5lByU0cYKT8KgjGGTbtvZ21+DTZZ7IrVv6fDWtjkqogrsvobrHXJ3l3ZmHhE\n"
"Im4W0pcF7tORmEYXRW9XaTGV7MQSEAraB6tXy7V86TxxnlJNwFxxB1rwBWlWXwtl\n"
"sF3slEW2x+UdZfOjGYoZztMDZRkt8g==\n"
"-----END CERTIFICATE-----\n";

static const char device_key_pem[] =
"-----BEGIN PRIVATE KEY-----\n"
"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\n"
"-----END PRIVATE KEY-----\n";

static int write_if_missing(sec_tag_t tag,
                            enum modem_key_mgmt_cred_type type,
                            const char *data)
{
    bool exists = false;
    int err = modem_key_mgmt_exists(tag, type, &exists);
    if (err) {
        printk("modem_key_mgmt_exists(type=%d) err=%d\n", type, err);
        return err;
    }

    if (exists) {
        printk("cred type %d already exists in sec_tag %d, skip\n", type, tag);
        return 0;
    }

    err = modem_key_mgmt_write(tag, type, data, strlen(data));
    printk("modem_key_mgmt_write(type=%d) -> %d\n", type, err);
    return err;
}

int provision_credentials(void)
{
    int err;

    err = write_if_missing(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, aws_root_ca_pem);
    if (err) {
        printk("Failed to provision CA chain: %d\n", err);
        return err;
    }

    err = write_if_missing(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT, device_cert_pem);
    if (err) {
        printk("Failed to provision public cert: %d\n", err);
        return err;
    }

    err = write_if_missing(SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT, device_key_pem);
    if (err) {
        printk("Failed to provision private key: %d\n", err);
        return err;
    }

    printk("credential provision done\n");
    return 0;
}
