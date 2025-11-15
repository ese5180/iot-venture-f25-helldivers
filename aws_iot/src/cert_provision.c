#include <zephyr/logging/log.h>
#include <modem/modem_key_mgmt.h>
#include <nrf_modem_at.h>
#include <string.h>

LOG_MODULE_REGISTER(cert_provision, CONFIG_LOG_DEFAULT_LEVEL);

/* 和 prj.conf 里的 CONFIG_MQTT_HELPER_SEC_TAG 保持一致 */
#define SEC_TAG 201

/* 直接把 PEM 放进代码里 */

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

static const char client_cert_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDWTCCAkGgAwIBAgIUQBzgqP0Ynawp+tItiU3QLigdU5EwDQYJKoZIhvcNAQEL\n"
"BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n"
"SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MTExNTIxMjAw\n"
"OFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n"
"ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMnFOOSqcA4/xm3cgzsW\n"
"vT8q2sxOA2J4Y6Yg/rxpO/aUuw+6VtrgDIsdCZe9MEv60+tVlvxSiJVTxDyd/0ut\n"
"iPOT7W13Z6DcVWeiPm0h2HQWGASNq7nR096xLTrWJJ0lSu960JP5RqLODk5Hve6n\n"
"6l8s8/mNEYnfTvf4xm/Ia7ULpIVHwMbe3jyiQ1SLo+DUpYVF4PaHH/LUDkg9R28y\n"
"aez0jZcMU7V9inzAUUEjaAPut7jmZGmCzf/gOca6mzGR+L3NzvrhceYxOs4QhfIe\n"
"QliEURbPn6n5Vu8DtreDwJ66C/GzGK1zQpixyzB9PYYHw1bZk86oIItJHL29lcGl\n"
"9PECAwEAAaNgMF4wHwYDVR0jBBgwFoAU0CahNXh0GCqQpn3OgPcHFbKuKYswHQYD\n"
"VR0OBBYEFBF1f7Zc7KKroPCk0Gvllzis9DCkMAwGA1UdEwEB/wQCMAAwDgYDVR0P\n"
"AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQAAvDz1YqFKc03AIn/82wKwSkJh\n"
"5eRVBi5fZQMbKO/iY7pcM0P2wDD3QTllgASMoh2Gg2yw4+5ec+RaupZYGFT3Ec5V\n"
"VuTqWtdOmF7ze96rXrvEVSLagDOvpJz96rqa3xebD2pnJ7azksSWN2hsBEkcH+2C\n"
"Aa87KjoVrwjnPB/KH/EjoBPyYKqmeHSS+h4WhJmf3zHlFMQ3ZSeCH/exBu08Y44b\n"
"j/2dGsjTJKWCq92RU0tXa/+ur4cskYUU9utW26FSZx1vWoQDym19LEd0cfkiLnJ7\n"
"FdSu/Sz5ix5e0xAttGuAowFzJ/3JeDI64JjP/b2pyqiXgS9iLKDakCxV64uG\n"
"-----END CERTIFICATE-----\n";


static const char client_key_pem[] =
"-----BEGIN PRIVATE KEY-----\n"
"MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDJxTjkqnAOP8Zt\n"
"3IM7Fr0/KtrMTgNieGOmIP68aTv2lLsPula24AyLHQmXvTBL+tPrVZb8UoiVU8Q8\n"
"nf9LrYjzk+1td2eg3FVnoj5tIdh0FhgEjau50dPesS061iSdJUrvetCT+Uaizg5O\n"
"R73up+pfLPP5jRGJ3073+MZv2Gu1C6SFR8DG3t48okNUi6Pg1KWFReD2hx/y1A5I\n"
"PUdvMmn s9I2XDFO1fYp8wFFBI2gD7re45mRpgs3/4DnGup sZHivc3O64XHmMTrOE\n"
"IXyHkJYhFEWz5+p+VbvA7a3g8Ceugu xsh rXNCmLHLMH09hgfDVtmTzqggi0kcvb2\n"
"VwaX08QIDAQABAoIBAHXAgrZ4fVLjh0NASNnoKGO/99b5xGHoTod6hA+i/pqmLBE+\n"
"t0efL9LUDHAguhntdVXHKolxBULYkxKpRn989ArrDsouwCyRycnkVrhL403EQxoN\n"
"L+YO/a3eCr5mkRGg3P2r2CQzezSyOokWt1Kbbl1DF9NQr6adyzFOX0iHV8xcyGrh\n"
"RHwFU2lujoe+1cDEXsTiMuJrcXm3xkCEt+ekZNHgBzmjLnedm6XiFJ2CCj+VaNEX\n"
"BZ7AB0mVjNzpK/LpNVFVrN+kFBmHTfkevQ2dF2CTd4SI7gCOOKio5UOh8UX9eBQx\n"
"zlwX8hj1/utMbunkqKXSI+FwL/xocriklcCv7JECgYEA790VMjZpBVbxJS6Pcpkk\n"
"w2bIyzcd0dYeUJF4G2piFkMh/dREi0LFDaZMPm1UFDQSOV1M3Q+2OxLMPzx/imDn\n"
"TkK14IolsMWg/3izeNIxi6XZDDlQdCkxxN6SybEwS5QblYbTKfJlU1Z4IpVz0LpK\n"
"nOiNQkaSGfPprTGKLf33mR0CgYEA11gZtyoFJg9DrpaqPGgyHKaOwcTbhxCNm6gC\n"
"9/t+oa3Yobpe8BnOTmBAPK1HVXYGkLG3ngCnJrevt1ljpEFeNNk4zsPwR1ukjKQo\n"
"y5TKEatVi/0UYYBqqLBBEayDcT14URkCYtjrBVjqPjTVe7hFVEAOJxyvL76gVVzb\n"
"VqXPluUCgYAlAnfCx6JssH2EvypjBD4n6DQTJu3y8pa617cwg7metb0I7TemRScid\n"
"AZPm47djytArqStdz8m3j+lgjArqcgGy0RE4Qvvuo6c3ILUoZoDchOLT0yan8COH\n"
"8mGVVCeWLvo1mS+lCgOM8lVjLnR+uyISVmCYGEqn/fuQTaQ4h7SAQKBgDtsp2QBJ\n"
"3ySN8Yk84NJ6ZI4cCqOjVnfOoSav375Gr+4/o/aozo0lNbR/sf/tTCjKjqhoB7s5T\n"
"Ns+wNomnOISlvhGakNLvAyHN1mq49KVty7YBcKVqZ1TnmQcdRTu83y/ZG7igoG/Av\n"
"0tmdGIydJ+W+/YWhvpPRS8WG9BxYJGZj1AoGAJGSXUyPhWuTRrAfzS023sBz9Wly\n"
"68kZQtySuOQo0+LE2LopiZvzfw5oEcTtpUE1F/VIDnJhXXe1c3/8KWg18A8nPq51b\n"
"1q2ycs7sBd51fSNK4FZgUQKCES0jGa2Pouq7CcPvkChQ6BlH1u5WkqW1OkA4cS4s\n"
"6eeF8/vtAXqZ2/I=\n"
"-----END PRIVATE KEY-----\n";

/* ========= 共用的小函数 ========= */

static int write_if_missing(nrf_sec_tag_t tag,
			    enum modem_key_mgmt_cred_type type,
			    const char *src)
{
	int err;
	bool exists;

	err = modem_key_mgmt_exists(tag, type, &exists);
	if (err) {
		LOG_ERR("modem_key_mgmt_exists(type=%d) err=%d", type, err);
		return err;
	}

	if (exists) {
		LOG_INF("Cred type %d already exists in sec tag %d", type, tag);
		return 0;
	}

	LOG_INF("Writing cred type %d to sec tag %d", type, tag);

	err = modem_key_mgmt_write(tag, type, src, strlen(src));
	if (err) {
		LOG_ERR("modem_key_mgmt_write(type=%d) err=%d", type, err);
		return err;
	}

	return 0;
}

/* ========= 对外接口：在 main 里调用一次 ========= */

int provision_credentials(void)
{
	int err;

	/* 1) Root CA */
	err = write_if_missing(SEC_TAG,
			       MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
			       aws_root_ca_pem);
	if (err) {
		LOG_ERR("Failed to provision CA chain: %d", err);
		return err;
	}

	/* 2) Client certificate */
	err = write_if_missing(SEC_TAG,
			       MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
			       client_cert_pem);
	if (err) {
		LOG_ERR("Failed to provision client public cert: %d", err);
		return err;
	}

	/* 3) Client private key */
	err = write_if_missing(SEC_TAG,
			       MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
			       client_key_pem);
	if (err) {
		LOG_ERR("Failed to provision client private key: %d", err);
		return err;
	}

	LOG_INF("Provisioning complete!");
	return 0;
}
