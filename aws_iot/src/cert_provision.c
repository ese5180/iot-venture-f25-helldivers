#include <zephyr/kernel.h>
#include <modem/modem_key_mgmt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cert_provision, LOG_LEVEL_INF);

#define AWS_SEC_TAG 201

/* ====== Your AWS Certificates ====== */

static const char root_ca[] =
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
"93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
"jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
"AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
"A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
"U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
"N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
"o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
"5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
"rqXRfboQnoZsG4q5WTP468SQvvG5\n"
"-----END CERTIFICATE-----\n";



static const char client_cert[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDWTCCAkGgAwIBAgIUfRIiGhr8/73rT+ubKv/+6jgNK8owDQYJKoZIhvcNAQEL\n"
"BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n"
"SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MTExODAwMjAx\n"
"MVoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n"
"ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANqUPspdcJdFUOHdpI+Q\n"
"bpyL6UEb1HdQcj5LjUkZT61nek3wpXI5YzYgrXEjK/FiJJppJZ5+U+B8x9ZtGPIo\n"
"Q7dD/t+j8l+CzFJOKorGT5k8PJVBPYMYWpfVINPq3x62waqvWDWtaMHub/FQEug1\n"
"FNAeynnaQPo9eas4aCDUS9y5hlFifVCoAUxZM6nSilPDFSAe9e0MyxAIXwf/N66F\n"
"RkUMwq3ilcYKB99+fNXhGKgz+Nc2h81QSLLAuUd2rgr1PVExxP4f7FmyHEVyDR15\n"
"PIQ4/UANN+our2rsGm71qO0mGf99CIaZ+yGYjdU3lSXRDbK4rA4Y2svJpTsabAh7\n"
"PXkCAwEAAaNgMF4wHwYDVR0jBBgwFoAUfKT0cOGSGRDzwA5YdV7cKl3PjXswHQYD\n"
"VR0OBBYEFFigHOlfVQd5pydbwRqSmBchyp8pMAwGA1UdEwEB/wQCMAAwDgYDVR0P\n"
"AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQCMNJtVndFhj1TbruOeV7QEU6rQ\n"
"694005dVZYTDp3+Z4vQWBwr9ZFmkZO6Q37PdTCQT4JZBzTdxHW8/auN2EO+b6n/5\n"
"AfMESaF+oXP/aoGCeZJp/j4cfPuIXQSUB0gy84y8ZHXRamFkfHSjAwJVkh84aEr2\n"
"6g3ueG2NgRv0KdMsuraRjUyPx8AGa/BEQkfTir9O7z+orAIfLVIfA63ZMIWr3hnA\n"
"JCrGqlSVpFGDoU7ys93LbAO3nZmi2xy0MzCykR2Ju/4OHEq667SWigOocNBO704T\n"
"HeeL0H072lWL2K/UNmk0FaoHWdnwHCjIqecp7aM80Zctvg9O6LdvKlWTr/+0\n"
"-----END CERTIFICATE-----\n";



static const char client_key[] =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEpAIBAAKCAQEA2pQ+yl1wl0VQ4d2kj5BunIvpQRvUd1ByPkuNSRlPrWd6TfCl\n"
"cjljNiCtcSMr8WIkmmklnn5T4HzH1m0Y8ihDt0P+36PyX4LMUk4qisZPmTw8lUE9\n"
"gxhal9Ug0+rfHrbBqq9YNa1owe5v8VAS6DUU0B7KedpA+j15qzhoINRL3LmGUWJ9\n"
"UKgBTFkzqdKKU8MVIB717QzLEAhfB/83roVGRQzCreKVxgoH33581eEYqDP41zaH\n"
"zVBIssC5R3auCvU9UTHE/h/sWbIcRXINHXk8hDj9QA036i6vauwabvWo7SYZ/30I\n"
"hpn7IZiN1TeVJdENsrisDhjay8mlOxpsCHs9eQIDAQABAoIBAHor+Di2J5N1u/Kz\n"
"JyLTcO/xR+wLeSNDhMeLBSqBikZ7GyJrSp5gszJy617ccNhXqevgr5U6OPm15SDW\n"
"E+ZuWQMbb19jTLrT6g5rAF5W3/DfeWFXeOFIgIJzLwkkM5gAQJl9rok6Jt6Wvl0c\n"
"C6Vc3ghB3ZxkQINeTx5DxffrkYeJ5re9kpdhWPTjXsf8mHmvLAXsMYASg7dkI6z0\n"
"g6N6Wa5YxqhEBX0ZHiTI7tkBfWJsa00jRbR8DoYNOCK2nx2xzUG7g+Ruu8leARjt\n"
"TfTxu7hsGzBz0hkA/7oyB4lH/xtJKIdPCYfvU1MBuVX4Hvl/eGWa9snuGorRILov\n"
"8tNmU70CgYEA7cGQhvrILw0F8K7bVLcrGhkB7ot7wvf+N3dl8KADqdACjDjpX8Ni\n"
"oUFjLzSkxAyULtLScSVXhv5dq9jUaZvHL+qcAj+ICTWZwqQ57XmSPm8jLsXwqrR5\n"
"Vce11GK2nq8VKpTQ9zLwItta9S6HCLwsvbE5KTjoS6xm4mv/K/DfRkMCgYEA61n4\n"
"iX68txEmL/gDioV8GuNi1c1RIRJmntbBkdzk6UrKXya+wolgT5rSS7VYty0Lb8wW\n"
"L/+hr3h35Caf9ldeyMhionbmU3KF2DGvYpiwEHfkNgfI4UqDSRutLghkTPQsdSsm\n"
"Dhrx4bTpeNAp9ILBfaCDuPJFIyKTLJ8XBawEt5MCgYEAnQXQwQTRvxlXyfyByLJs\n"
"WgEYIIaohzyn/dPyVHgp0zYY6KkRoHh3JE7+BYg8JWq1VzNPXwCtjO9jyAIdT15d\n"
"sAy/WlDBNGvdEArMY0V+S5O05cg+yU8GL5wFP66uM6EoVoYQeKKArFS4/uLqtd8p\n"
"oWJxgQMfkBLdpD51OX0MyOcCgYBif1EJGkWYyFCXGtFAlAUQq8GdgURG9xCDwZVZ\n"
"mn0jNe3kTK7hc+Ue68i/brImV2/F5kAS7oWYjm5+ybuAuagtl8/P9rsGiZOCm6iv\n"
"Nv/YpJSaGD/2Kd0wJ6ke7twraLWChAB1PsmkkLZ1nYkxj9ey4A7AxIQy5DF8J0jX\n"
"UtrWiQKBgQDLbTrVZUAwA5SeC28qBJkPywV+b4sji5yQjI3e4euoveAGZLdmQ9cf\n"
"ekNrmP5LXtNWwqZUxjpBFOmBEr3HjAK+XWouUa5y2Lf0ttyLfho7qO9CauFbCfFL\n"
"7hMGg8+2qoXIZhOMfnWgI0EfFQ28yOuzSa8B5Ej3uDoV8fvj5cB7zA==\n"
"-----END RSA PRIVATE KEY-----\n";



/* ====== Helper: check if credential exists ====== */
static bool cred_exists(uint16_t tag, enum modem_key_mgmt_cred_type type)
{
    size_t len = 0;
    int err = modem_key_mgmt_read(tag, type, NULL, &len);

    return (err == 0 && len > 0);
}

/* ====== Provision one credential if missing ====== */
static int write_if_missing(uint16_t tag,
                            enum modem_key_mgmt_cred_type type,
                            const char *pem)
{
    if (cred_exists(tag, type)) {
        LOG_INF("Credential type %d already exists. Skipping.", type);
        return 0;
    }

    LOG_INF("Writing credential type %d ...", type);
    return modem_key_mgmt_write(tag, type, pem, strlen(pem));
}

/* ====== Public function called by main() ====== */
int provision_credentials(void)
{
    int err;

	/* 1) Root CA */
	err = write_if_missing(AWS_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
		root_ca);

	/* 2) Client certificate */
	err = write_if_missing(AWS_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
		client_cert);

	/* 3) Private key */
	err = write_if_missing(AWS_SEC_TAG,
		MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
		client_key);

    if (err) return err;

    LOG_INF("All AWS credentials provisioned successfully.");
    return 0;
}
