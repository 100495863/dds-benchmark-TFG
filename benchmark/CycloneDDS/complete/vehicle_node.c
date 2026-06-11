#include "dds/dds.h"
#include "vehicle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef enum { MODE_TELEMETRY, MODE_EMERGENCY, MODE_LIDAR } test_mode_t;
typedef enum { ROLE_PUB, ROLE_SUB } node_role_t;

test_mode_t mode = MODE_TELEMETRY;
node_role_t role = ROLE_PUB;
uint32_t payload_size = 2 * 1024 * 1024;
char node_id[32] = "Node_1";

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "telemetry") == 0) mode = MODE_TELEMETRY;
            else if (strcmp(argv[i+1], "emergency") == 0) mode = MODE_EMERGENCY;
            else if (strcmp(argv[i+1], "lidar") == 0) mode = MODE_LIDAR;
            i++;
        } else if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "pub") == 0) role = ROLE_PUB;
            else if (strcmp(argv[i+1], "sub") == 0) role = ROLE_SUB;
            i++;
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            payload_size = atoi(argv[i+1]);
            i++;
        } else if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            strncpy(node_id, argv[i+1], sizeof(node_id) - 1);
            i++;
        }
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    parse_args(argc, argv);
    printf("=== Arrancando | Modo: %d | Rol: %d | ID: %s ===\n", mode, role, node_id);

    dds_entity_t participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0) DDS_FATAL("Error participante: %s\n", dds_strretcode(-participant));

    dds_entity_t topic, writer, reader;
    dds_qos_t *qos = dds_create_qos();

    // ==========================================
    // ESCENARIO 1: TELEMETRÍA (Escalabilidad)
    // ==========================================
    if (mode == MODE_TELEMETRY) {
        topic = dds_create_topic(participant, &V2X_VehicleTelemetry_desc, "TelemetryTopic", NULL, NULL);
        
        if (role == ROLE_PUB) {
            writer = dds_create_writer(participant, topic, NULL, NULL);
            V2X_VehicleTelemetry msg;
            msg.vehicle_id = node_id;
            
            while (1) {
                msg.timestamp_ns = now_ns();
                msg.latitude = 40.0; msg.longitude = -3.0; msg.speed = 120.5; msg.heading = 90.0;
                dds_write(writer, &msg);
                dds_sleepfor(DDS_MSECS(100));
            }
        } else {
            reader = dds_create_reader(participant, topic, NULL, NULL);
            void *samples[1];
            dds_sample_info_t infos[1];
            samples[0] = V2X_VehicleTelemetry__alloc();
            
            while (1) {
                if (dds_take(reader, samples, infos, 1, 1) > 0 && infos[0].valid_data) {
                    V2X_VehicleTelemetry *msg = (V2X_VehicleTelemetry*)samples[0];
                    printf("[TELEMETRY] Recibido de %s\n", msg->vehicle_id);
                }
                dds_sleepfor(DDS_MSECS(10));
            }
        }
    }
    
    // ==========================================
    // ESCENARIO 2: EMERGENCY (QoS y Caos de Red)
    // ==========================================
    else if (mode == MODE_EMERGENCY) {
        dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
        dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
	dds_qset_history(qos, DDS_HISTORY_KEEP_ALL, 10);
	dds_qset_durability_service(
            qos,
            DDS_SECS(0),
            DDS_HISTORY_KEEP_ALL,
            DDS_LENGTH_UNLIMITED,
            DDS_LENGTH_UNLIMITED,
            DDS_LENGTH_UNLIMITED,
            DDS_LENGTH_UNLIMITED
        );
        
        topic = dds_create_topic(participant, &V2X_EmergencyBrakeAlert_desc, "EmergencyTopic", NULL, NULL);
        
        if (role == ROLE_PUB) {
            writer = dds_create_writer(participant, topic, qos, NULL);
            V2X_EmergencyBrakeAlert msg;
            msg.alert_id = "ALERT_001";
            msg.sender_id = node_id;
            msg.timestamp_ns = now_ns();
            msg.is_active = true;
            
            printf("[EMERGENCY] Publicando alerta CRÍTICA (Se quedará guardada por el QoS)...\n");
            for(int i=0; i<5; i++) {
                dds_write(writer, &msg);
                dds_sleepfor(DDS_MSECS(100));
            }
            while(1) dds_sleepfor(DDS_SECS(1)); 
        } else {
            reader = dds_create_reader(participant, topic, qos, NULL);
            void *samples[1];
            dds_sample_info_t infos[1];
            samples[0] = V2X_EmergencyBrakeAlert__alloc();
            
            printf("[EMERGENCY] Suscriptor iniciado. Esperando mensajes (incluso pasados)...\n");
            while (1) {
                if (dds_take(reader, samples, infos, 1, 1) > 0 && infos[0].valid_data) {
                    uint64_t latency = now_ns() - ((V2X_EmergencyBrakeAlert*)samples[0])->timestamp_ns;
                    printf("[ALERTA RECIBIDA] de %s | Latencia real o de historial: %.2f ms\n", 
                           ((V2X_EmergencyBrakeAlert*)samples[0])->sender_id, latency / 1000000.0);
                }
                dds_sleepfor(DDS_MSECS(10));
            }
        }
    }

    // ==========================================
    // ESCENARIO 3: LIDAR (Throughput / Fragmentación)
    // ==========================================
    else if (mode == MODE_LIDAR) {
        topic = dds_create_topic(participant, &V2X_LiDARPointCloud_desc, "LidarTopic", NULL, NULL);
        
        if (role == ROLE_PUB) {
            writer = dds_create_writer(participant, topic, NULL, NULL);
            V2X_LiDARPointCloud *msg = V2X_LiDARPointCloud__alloc();
            msg->sensor_id = node_id;
            msg->sequence_number = 0;
            
            msg->data._buffer = dds_alloc(payload_size);
            msg->data._length = payload_size;
            msg->data._maximum = payload_size;
            memset(msg->data._buffer, 0xAA, payload_size);
            
            printf("[LIDAR] Publicando frames de %.2f MB a máxima velocidad...\n", payload_size / 1024.0 / 1024.0);
            while (1) {
                msg->timestamp_ns = now_ns();
                msg->sequence_number++;
                dds_write(writer, msg);
            }
        } else {
            reader = dds_create_reader(participant, topic, NULL, NULL);
            void *samples[1];
            dds_sample_info_t infos[1];
            samples[0] = V2X_LiDARPointCloud__alloc();
            
            uint64_t total_bytes = 0;
            uint64_t start_time = now_ns();
            
            while (1) {
                int n = dds_take(reader, samples, infos, 1, 1);
                if (n > 0 && infos[0].valid_data) {
                    V2X_LiDARPointCloud *msg = (V2X_LiDARPointCloud*)samples[0];
                    total_bytes += msg->data._length;

                    uint64_t elapsed = now_ns() - start_time;
                    if (elapsed >= 1000000000ULL) { // 1 segundo
                        double mbps = (double)total_bytes / (1024.0 * 1024.0);
                        printf("[LIDAR] Throughput: %.2f MB/s\n", mbps);
                        total_bytes = 0;
                        start_time = now_ns();
                    }
                } else {
                    // Solo dormimos 1ms si no había nada en la cola para no quemar CPU a lo tonto
                    dds_sleepfor(DDS_MSECS(1));
                }
            }
        }
    }

    dds_delete_qos(qos);
    return 0;
}
