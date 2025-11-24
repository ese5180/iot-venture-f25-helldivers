#ifndef SENSOR_H
#define SENSOR_H

typedef enum {
    STATE_NORMAL = 0,
    STATE_LEFT,
    STATE_RIGHT,
    STATE_FRONT,
    STATE_HIND
} balance_state_t;
extern float g_temperature;
extern float g_humidity;
extern float g_pressure;
extern float g_roll;
extern float g_pitch;
extern balance_state_t g_state;
/* 初始化 */
void sensor_init(void);

/* 获取传感器数值 */
float sensor_get_temperature(void);
float sensor_get_humidity(void);
float sensor_get_pressure(void);
float sensor_get_roll(void);
float sensor_get_pitch(void);
balance_state_t sensor_get_state(void);

#endif /* SENSOR_H */
