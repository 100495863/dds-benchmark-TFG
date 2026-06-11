#include "vehicleTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/SubscriberImpl.h>
#include <dds/DCPS/StaticIncludes.h>
#include <ace/OS_NS_unistd.h>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <string>
#include <cstring>

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

void parse_args(int argc, ACE_TCHAR **argv) {
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

int ACE_TMAIN(int argc, ACE_TCHAR* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    try {
        // Inicializamos participante
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        
        parse_args(argc, argv);
        std::cout << "=== [NODE OpenDDS] Arrancando | Modo: " << mode 
                  << " | Rol: " << role << " | ID: " << node_id << " ===" << std::endl;

        DDS::DomainParticipant_var participant = dpf->create_participant(0, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        if (!participant) return 1;

        DDS::Publisher_var publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::Subscriber_var subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // ==========================================
        // ESCENARIO 1: Escalabilidad
        // ==========================================
        if (mode == MODE_TELEMETRY) {
            V2X::VehicleTelemetryTypeSupport_var ts = new V2X::VehicleTelemetryTypeSupportImpl();
            ts->register_type(participant, "");
            CORBA::String_var type_name = ts->get_type_name();
            DDS::Topic_var topic = participant->create_topic("TelemetryTopic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

            if (role == ROLE_PUB) {
                DDS::DataWriter_var dw = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
                V2X::VehicleTelemetryDataWriter_var writer = V2X::VehicleTelemetryDataWriter::_narrow(dw);
                
                V2X::VehicleTelemetry msg;
                msg.vehicle_id = node_id.c_str();

                while (true) {
                    msg.timestamp_ns = now_ns();
                    msg.latitude = 40.0; msg.longitude = -3.0; msg.speed = 120.5; msg.heading = 90.0;
                    writer->write(msg, DDS::HANDLE_NIL);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            } else {
                DDS::DataReader_var dr = subscriber->create_datareader(topic, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
                V2X::VehicleTelemetryDataReader_var reader = V2X::VehicleTelemetryDataReader::_narrow(dr);
                
                V2X::VehicleTelemetry msg;
                DDS::SampleInfo info;

                while (true) {
                    if (reader->take_next_sample(msg, info) == DDS::RETCODE_OK && info.valid_data) {
                        std::cout << "[TELEMETRY] Recibido de " << msg.vehicle_id.in() << std::endl;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
        // ==========================================
        // ESCENARIO 2: QoS
        // ==========================================
        else if (mode == MODE_EMERGENCY) {
            V2X::EmergencyBrakeAlertTypeSupport_var ts = new V2X::EmergencyBrakeAlertTypeSupportImpl();
            ts->register_type(participant, "");
            CORBA::String_var type_name = ts->get_type_name();
            DDS::Topic_var topic = participant->create_topic("EmergencyTopic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

            if (role == ROLE_PUB) {
                DDS::DataWriterQos wqos;
                publisher->get_default_datawriter_qos(wqos);
                wqos.durability.kind = DDS::TRANSIENT_LOCAL_DURABILITY_QOS;
                wqos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;
		wqos.history.kind = DDS::KEEP_LAST_HISTORY_QOS;
                wqos.history.depth = 10;

                DDS::DataWriter_var dw = publisher->create_datawriter(topic, wqos, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
                V2X::EmergencyBrakeAlertDataWriter_var writer = V2X::EmergencyBrakeAlertDataWriter::_narrow(dw);

                V2X::EmergencyBrakeAlert msg;
                msg.alert_id = "ALERT_001";
                msg.sender_id = node_id.c_str();
                msg.is_active = true;

                std::cout << "[EMERGENCY] Publicando alerta CRÍTICA (QoS Transient Local)...\n";
		std::this_thread::sleep_for(std::chrono::seconds(1));
                for(int i=0; i<5; i++) {
                    msg.timestamp_ns = now_ns();
                    writer->write(msg, DDS::HANDLE_NIL);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                while(true) std::this_thread::sleep_for(std::chrono::seconds(1));
            } else {
                DDS::DataReaderQos rqos;
                subscriber->get_default_datareader_qos(rqos);
                rqos.durability.kind = DDS::TRANSIENT_LOCAL_DURABILITY_QOS;
                rqos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;
		rqos.history.kind = DDS::KEEP_LAST_HISTORY_QOS;
                rqos.history.depth = 10;

                DDS::DataReader_var dr = subscriber->create_datareader(topic, rqos, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
                V2X::EmergencyBrakeAlertDataReader_var reader = V2X::EmergencyBrakeAlertDataReader::_narrow(dr);

                V2X::EmergencyBrakeAlert msg;
                DDS::SampleInfo info;

                std::cout << "[EMERGENCY] Suscriptor iniciado. Esperando mensajes...\n";
                while (true) {
                    if (reader->take_next_sample(msg, info) == DDS::RETCODE_OK && info.valid_data) {
                        double latency = (now_ns() - msg.timestamp_ns) / 1000000.0;
                        std::cout << "[ALERTA RECIBIDA] de " << msg.sender_id.in() 
                                  << " | Latencia real o de historial: " 
                                  << std::fixed << std::setprecision(2) << latency << " ms\n";
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
        // ==========================================
        // ESCENARIO 3: Throughput
        // ==========================================
        else if (mode == MODE_LIDAR) {
            V2X::LiDARPointCloudTypeSupport_var ts = new V2X::LiDARPointCloudTypeSupportImpl();
            ts->register_type(participant, "");
            CORBA::String_var type_name = ts->get_type_name();
            DDS::Topic_var topic = participant->create_topic("LidarTopic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

            if (role == ROLE_PUB) {
                DDS::DataWriter_var dw = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
                V2X::LiDARPointCloudDataWriter_var writer = V2X::LiDARPointCloudDataWriter::_narrow(dw);

                V2X::LiDARPointCloud msg;
                msg.sensor_id = node_id.c_str();
                msg.sequence_number = 0;
                
                // Asignar tamaño a la carga enviada
                msg.data.length(payload_size);
                for(uint32_t i=0; i<payload_size; ++i) msg.data[i] = 0xAA;

                std::cout << "[LIDAR] Publicando frames de " << (payload_size / 1024.0 / 1024.0) << " MB a máxima velocidad...\n";
                while (true) {
                    msg.timestamp_ns = now_ns();
                    msg.sequence_number++;
                    writer->write(msg, DDS::HANDLE_NIL);
                }
            } else {
                DDS::DataReader_var dr = subscriber->create_datareader(topic, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
                V2X::LiDARPointCloudDataReader_var reader = V2X::LiDARPointCloudDataReader::_narrow(dr);

                V2X::LiDARPointCloud msg;
                DDS::SampleInfo info;

                uint64_t total_bytes = 0;
                uint64_t start_time = now_ns();

                while (true) {
                    if (reader->take_next_sample(msg, info) == DDS::RETCODE_OK && info.valid_data) {
                        total_bytes += msg.data.length();

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
    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception caught in vehicle_node:");
        return 1;
    }
    return 0;
}
