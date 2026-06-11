#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <cstring>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/Topic.hpp>

// Cabeceras generadas por fastddsgen
#include "vehiclePubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

enum TestMode { MODE_TELEMETRY, MODE_EMERGENCY, MODE_LIDAR };
enum NodeRole { ROLE_PUB, ROLE_SUB };

TestMode mode = MODE_TELEMETRY;
NodeRole role = ROLE_PUB;
uint32_t payload_size = 2 * 1024 * 1024;
std::string node_id = "Node_1";

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "telemetry") mode = MODE_TELEMETRY;
            else if (val == "emergency") mode = MODE_EMERGENCY;
            else if (val == "lidar") mode = MODE_LIDAR;
        } else if (arg == "--role" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "pub") role = ROLE_PUB;
            else if (val == "sub") role = ROLE_SUB;
        } else if (arg == "--payload" && i + 1 < argc) {
            payload_size = std::stoi(argv[++i]);
        } else if (arg == "--id" && i + 1 < argc) {
            node_id = argv[++i];
        }
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    parse_args(argc, argv);
    std::cout << "=== NODE FastDDS Arrancando | Modo: " << mode
              << " | Rol: " << role << " | ID: " << node_id << " ===" << std::endl;

    // DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;

    // Inyectamos la propiedad que habilita los "tópicos ocultos" de telemetría interna
    pqos.properties().properties().emplace_back("fastdds.statistics",
        "HISTORY_LATENCY_TOPIC;"
        "NETWORK_LATENCY_TOPIC;"
        "DISCOVERY_TOPIC;"
        "PHYSICAL_DATA_TOPIC");

    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, pqos);

    Publisher* publisher = nullptr;
    Subscriber* subscriber = nullptr;
    if (role == ROLE_PUB) publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
    else subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);

    // ==========================================
    // ESCENARIO 1: TELEMETRÍA (Escalabilidad)
    // ==========================================
    if (mode == MODE_TELEMETRY) {
        TypeSupport type(new V2X::VehicleTelemetryPubSubType());
        type.register_type(participant);
        Topic* topic = participant->create_topic("TelemetryTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);

        if (role == ROLE_PUB) {
            DataWriter* writer = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);
            V2X::VehicleTelemetry msg;
            msg.vehicle_id(node_id);

            while (true) {
                msg.timestamp_ns(now_ns());
                msg.latitude(40.0); msg.longitude(-3.0); msg.speed(120.5); msg.heading(90.0);
                writer->write(&msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            DataReader* reader = subscriber->create_datareader(topic, DATAREADER_QOS_DEFAULT);
            V2X::VehicleTelemetry msg;
            SampleInfo info;

            while (true) {
                if (reader->take_next_sample(&msg, &info) == RETCODE_OK && info.valid_data) {
                    std::cout << "[TELEMETRY] Recibido de " << msg.vehicle_id() << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    // ==========================================
    // ESCENARIO 2: EMERGENCY (QoS y Caos de Red)
    // ==========================================
    else if (mode == MODE_EMERGENCY) {
        TypeSupport type(new V2X::EmergencyBrakeAlertPubSubType());
        type.register_type(participant);
        Topic* topic = participant->create_topic("EmergencyTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);

        if (role == ROLE_PUB) {
            DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;
            wqos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;
            wqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
            DataWriter* writer = publisher->create_datawriter(topic, wqos);

            V2X::EmergencyBrakeAlert msg;
            msg.alert_id("ALERT_001");
            msg.sender_id(node_id);
            msg.is_active(true);

            std::cout << "[EMERGENCY] Publicando alerta CRÍTICA (QoS Transient Local)...\n";
            for(int i=0; i<5; i++) {
                msg.timestamp_ns(now_ns());
                writer->write(&msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            while(true) std::this_thread::sleep_for(std::chrono::seconds(1));
        } else {
            DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
            rqos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;
            rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
            DataReader* reader = subscriber->create_datareader(topic, rqos);

            V2X::EmergencyBrakeAlert msg;
            SampleInfo info;

            std::cout << "[EMERGENCY] Suscriptor iniciado. Esperando mensajes...\n";
            while (true) {
                if (reader->take_next_sample(&msg, &info) == RETCODE_OK && info.valid_data) {
                    double latency = (now_ns() - msg.timestamp_ns()) / 1000000.0;
                    std::cout << "[ALERTA RECIBIDA] de " << msg.sender_id()
                              << " | Latencia real o de historial: "
                              << std::fixed << std::setprecision(2) << latency << " ms\n";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    // ==========================================
    // ESCENARIO 3: LIDAR (Throughput / Fragmentación)
    // ==========================================
    else if (mode == MODE_LIDAR) {
        TypeSupport type(new V2X::LiDARPointCloudPubSubType());
        type.register_type(participant);
        Topic* topic = participant->create_topic("LidarTopic", type.get_type_name(), TOPIC_QOS_DEFAULT);

        if (role == ROLE_PUB) {
            DataWriter* writer = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);
            V2X::LiDARPointCloud msg;
            msg.sensor_id(node_id);
            msg.sequence_number(0);

            msg.data().resize(payload_size, 0xAA);

            std::cout << "[LIDAR] Publicando frames de " << (payload_size / 1024.0 / 1024.0) << " MB a máxima velocidad...\n";
            while (true) {
                msg.timestamp_ns(now_ns());
                msg.sequence_number(msg.sequence_number() + 1);
                writer->write(&msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        } else {
            DataReader* reader = subscriber->create_datareader(topic, DATAREADER_QOS_DEFAULT);
            V2X::LiDARPointCloud msg;
            SampleInfo info;

            uint64_t total_bytes = 0;
            uint64_t start_time = now_ns();

            while (true) {
                if (reader->take_next_sample(&msg, &info) == RETCODE_OK && info.valid_data) {
                    total_bytes += msg.data().size();

                    uint64_t elapsed = now_ns() - start_time;
                    if (elapsed >= 1000000000ULL) {
                        double mbps = (double)total_bytes / (1024.0 * 1024.0);
                        std::cout << "[LIDAR] Throughput: " << std::fixed << std::setprecision(2) << mbps << " MB/s\n";
                        total_bytes = 0;
                        start_time = now_ns();
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    return 0;
}
