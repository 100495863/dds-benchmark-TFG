#include <chrono>
#include <thread>
#include <cmath>
#include <iostream>
#include <iomanip>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/Topic.hpp>

#include "dronPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

int main() {
    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);

    TypeSupport type_imu(new Icarus::IMUDataPubSubType());
    type_imu.register_type(participant);

    TypeSupport type_env(new Icarus::SystemStatusPubSubType());
    type_env.register_type(participant);

    Topic* topic_imu = participant->create_topic("IMU_Topic", type_imu.get_type_name(), TOPIC_QOS_DEFAULT);
    Topic* topic_env = participant->create_topic("Env_Topic", type_env.get_type_name(), TOPIC_QOS_DEFAULT);

    Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);

    DataReaderQos qos_imu = DATAREADER_QOS_DEFAULT;
    qos_imu.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
    DataReader* reader_imu = subscriber->create_datareader(topic_imu, qos_imu);

    DataReaderQos qos_env = DATAREADER_QOS_DEFAULT;
    qos_env.reliability().kind = RELIABLE_RELIABILITY_QOS;
    DataReader* reader_env = subscriber->create_datareader(topic_env, qos_env);

    std::cout << "=== [FLIGHT CONTROLLER FastDDS] Iniciando... Monitorizando IMU y Batería" << std::endl;

    Icarus::IMUData msg_imu;
    Icarus::SystemStatus msg_env;
    SampleInfo info;

    float current_battery = 100.0f;
    bool emergency_landing = false;
    int log_counter = 0;

    while (true) {
        if (reader_imu->take_next_sample(&msg_imu, &info) == RETCODE_OK) {
            if (info.valid_data) {
                float x = msg_imu.accel_x();
                float y = msg_imu.accel_y();
                float z = msg_imu.accel_z();

                float total_accel = std::sqrt((x*x) + (y*y) + (z*z));

                if (emergency_landing) {
                    if (++log_counter % 50 == 0) {
                        std::cout << "[FCC] !!! ATERRIZAJE DE EMERGENCIA !!! Altitud descendiendo..." << std::endl;
                    }
                } else {
                    float disturbance = std::abs(total_accel - 9.81f);
                    if (disturbance > 2.0f) {
                         std::cout << "[ALERTA] TURBULENCIA DETECTADA! G: " 
                                   << std::fixed << std::setprecision(2) << total_accel 
                                   << " -> CORRIGIENDO MOTORES" << std::endl;
                    }
                }
            }
        }

        if (reader_env->take_next_sample(&msg_env, &info) == RETCODE_OK) {
            if (info.valid_data) {
                current_battery = msg_env.battery_level();

                std::cout << "[SISTEMA] Estado recibido -> Batería: " << current_battery 
                          << "% | GPS: " << (msg_env.gps_valid() ? "OK" : "NO_FIX")
                          << " | Lat: " << msg_env.latitude() << std::endl;

                if (current_battery < 20.0f && !emergency_landing) {
                    std::cout << "[ALARMA] BATERÍA CRÍTICA (<20%). INICIANDO PROTOCOLO DE RETORNO (RTL)." << std::endl;
                    emergency_landing = true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
