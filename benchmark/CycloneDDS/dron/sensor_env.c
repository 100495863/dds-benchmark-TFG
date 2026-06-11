#include "dds/dds.h"
#include "dron.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(int argc, char **argv) {
    dds_entity_t participant, topic, writer;
    Icarus_SystemStatus msg;

    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    topic = dds_create_topic(participant, &Icarus_SystemStatus_desc, "SystemStatus_Topic", NULL, NULL);
    writer = dds_create_writer(participant, topic, NULL, NULL);

    printf("=== [SENSOR ENV] Iniciando simulación GPS/Batería a 1Hz...\n");

    msg.system_id = "PowerUnit_1";
    msg.battery_level = 100.0f;
    msg.altitude = 50.0f;
    msg.latitude = 40.4168;
    msg.longitude = -3.7038;

    while (1) {
        msg.timestamp_ns = now_ns();
        msg.battery_level -= 0.5f; 
        if (msg.battery_level < 0) msg.battery_level = 100.0f;

        msg.longitude += 0.0001;
        msg.gps_valid = (msg.battery_level > 10.0f);

        dds_write(writer, &msg);

        dds_sleepfor(DDS_MSECS(1000));
    }
    return 0;
}
