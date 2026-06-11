#include "dds/dds.h"
#include "dron.h"
#include <stdio.h>
#include <math.h>

#define MAX_SAMPLES 1

int main(int argc, char **argv) {
    dds_entity_t participant;
    dds_entity_t topic_imu, reader_imu;
    dds_entity_t topic_env, reader_env;

    void *samples_imu[MAX_SAMPLES];
    dds_sample_info_t infos_imu[MAX_SAMPLES];

    void *samples_env[MAX_SAMPLES];
    dds_sample_info_t infos_env[MAX_SAMPLES];

    dds_return_t rc;

    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);

    topic_imu = dds_create_topic(participant, &Icarus_IMUData_desc, "IMU_Topic", NULL, NULL);
    reader_imu = dds_create_reader(participant, topic_imu, NULL, NULL);

    topic_env = dds_create_topic(participant, &Icarus_SystemStatus_desc, "SystemStatus_Topic", NULL, NULL);
    reader_env = dds_create_reader(participant, topic_env, NULL, NULL);

    samples_imu[0] = Icarus_IMUData__alloc();
    samples_env[0] = Icarus_SystemStatus__alloc();

    printf("=== [FLIGHT CONTROLLER] Iniciando... Monitorizando IMU (100Hz) y Batería (1Hz)\n");

    float current_battery = 100.0f;
    int emergency_landing = 0;

    while (1) {
        rc = dds_take(reader_imu, samples_imu, infos_imu, MAX_SAMPLES, MAX_SAMPLES);
        
        if (rc > 0 && infos_imu[0].valid_data) {
            Icarus_IMUData *msg = (Icarus_IMUData*)samples_imu[0];

            float total_accel = sqrtf(
                (msg->accel_x * msg->accel_x) + 
                (msg->accel_y * msg->accel_y) + 
                (msg->accel_z * msg->accel_z)
            );

            if (emergency_landing) {
                static int land_count = 0;
                if (++land_count % 50 == 0) printf("[FCC] !!! ATERRIZAJE DE EMERGENCIA EN PROGRESO !!! Altitud descendiendo...\n");
            } 
            else {
                float disturbance = fabsf(total_accel - 9.81f);
                if (disturbance > 2.0f) {
                    printf("[ALERTA] TURBULENCIA DETECTADA (G: %.2f) -> CORRIGIENDO MOTORES!\n", total_accel);
                }
            }
        }

        rc = dds_take(reader_env, samples_env, infos_env, MAX_SAMPLES, MAX_SAMPLES);

        if (rc > 0 && infos_env[0].valid_data) {
            Icarus_SystemStatus *msg = (Icarus_SystemStatus*)samples_env[0];
            current_battery = msg->battery_level;

            printf("[SISTEMA] Estado recibido -> Batería: %.1f%% | GPS: %s | Lat: %.4f\n", 
                   current_battery, 
                   msg->gps_valid ? "OK" : "NO_FIX", 
                   msg->latitude);

            if (current_battery < 20.0f && !emergency_landing) {
                printf("[ALARMA] BATERÍA CRÍTICA (<20%%). INICIANDO PROTOCOLO DE RETORNO (RTL).\n");
                emergency_landing = 1;
            }
        }

        dds_sleepfor(DDS_MSECS(1));
    }

    dds_free(samples_imu[0]);
    dds_free(samples_env[0]);
    return 0;
}
