#ifndef HORSE_PAYLOAD_H__
#define HORSE_PAYLOAD_H__

#include <stdint.h>

struct horse_payload {
    int64_t water_flag; 
    int64_t water_time;    
    int32_t temperature;  // scaled by 100
    int32_t moisture;     // scaled by 100
    int32_t pitch;        // scaled by 100
    int32_t latitude;     // scaled by 1e6
    int32_t longitude;    // scaled by 1e6
};

int horse_payload_construct(char *msg, size_t size, struct horse_payload *payload);

#endif
