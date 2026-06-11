#include "dds/dds.h"
#include "dron.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(int argc, char **argv) {
    dds_entity_t participant, topic, writer;
    dds_return_t rc;
    Icarus_IMUData msg;

    srand(time(NULL));

    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0) DDS_FATAL("participant: %s\n", dds_strretcode(-participant));

    topic = dds_create_topic(participant, &Icarus_IMUData_desc, "IMU_Topic", NULL, NULL);
    if (topic < 0) DDS_FATAL("topic: %s\n", dds_strretcode(-topic));

    writer = dds_create_writer(participant, topic, NULL, NULL);
    if (writer < 0) DDS_FATAL("writer: %s\n", dds_strretcode(-writer));

    printf("=== [SENSOR IMU] Iniciando simulación (Vuelo Estable con ráfagas ocasionales)...\n");

    msg.sensor_id = "IMU_Main";
    float t = 0.0f;

    int turbulence_cooldown = 0;

    while (1) {
        msg.timestamp_ns = now_ns();
        t += 0.1f;

        msg.accel_x = 0.5f * sinf(t);
        msg.accel_y = 0.5f * cosf(t);
        msg.accel_z = 9.81f; 

        if (turbulence_cooldown > 0) {
            turbulence_cooldown--;
            msg.accel_z += ((float)rand()/RAND_MAX * 0.2f) - 0.1f;
        }
        else {
            int dice = rand() % 300; 

            if (dice == 0) {
                float sign = (rand() % 2 == 0) ? 1.0f : -1.0f;
                float force = 3.0f + ((float)rand() / RAND_MAX * 3.0f);
                float spike = force * sign;

                msg.accel_z += spike;
                msg.accel_x += spike / 3.0f; 

                printf(">>> [SENSOR] !! GENERANDO RAFAGA !!: %.2f G\n", spike);

                turbulence_cooldown = 200; 
            } else {
                msg.accel_z += ((float)rand()/RAND_MAX * 0.2f) - 0.1f;
            }
        }

        msg.gyro_x = 0.1f * sinf(t/2.0f);
        msg.gyro_y = 0.1f * cosf(t/2.0f);
        msg.gyro_z = 0.0f;

        rc = dds_write(writer, &msg);
        if (rc != DDS_RETCODE_OK) DDS_FATAL("write failed");

        dds_sleepfor(DDS_MSECS(10));
    }

    return 0;
}
