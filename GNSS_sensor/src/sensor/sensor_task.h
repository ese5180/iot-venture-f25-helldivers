#ifndef SENSOR_TASK_H_
#define SENSOR_TASK_H_

#include "horse_balance.h"

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float bno_heading;
    float bno_roll;
    float bno_pitch;
    hb_state_t horse_state;
} sensor_data_msg_t;

void sensor_task_start(void);
void sensor_task_get_data(sensor_data_msg_t *out);

#endif
