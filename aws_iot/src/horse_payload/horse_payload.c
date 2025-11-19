#include <zephyr/logging/log.h>
#include <zephyr/data/json.h>
#include "horse_payload.h"

LOG_MODULE_REGISTER(horse_payload, LOG_LEVEL_INF);

static const struct json_obj_descr horse_desc[] = {
    //JSON_OBJ_DESCR_PRIM(struct horse_payload, timestamp,    JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct horse_payload, temperature,  JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct horse_payload, moisture,     JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct horse_payload, pitch,        JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct horse_payload, latitude,     JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct horse_payload, longitude,    JSON_TOK_NUMBER),
};

int horse_payload_construct(char *msg, size_t size, struct horse_payload *payload)
{
    int ret = json_obj_encode_buf(horse_desc,
                                  ARRAY_SIZE(horse_desc),
                                  payload,
                                  msg,
                                  size);

    if (ret) {
        LOG_ERR("JSON encode error: %d", ret);
    }

    return ret;
}
