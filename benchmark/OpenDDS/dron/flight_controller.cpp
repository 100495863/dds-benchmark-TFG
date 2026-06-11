#include "dronTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/SubscriberImpl.h>
#include <dds/DCPS/StaticIncludes.h>
#include <ace/OS_NS_unistd.h>
#include <ace/streams.h>
#include <cmath>
#include <iomanip>

int ACE_TMAIN(int argc, ACE_TCHAR* argv[])
{
    try {
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        DDS::DomainParticipant_var participant = dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        if (!participant) return 1;

        Icarus::IMUDataTypeSupport_var ts_imu = new Icarus::IMUDataTypeSupportImpl();
        if (ts_imu->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        Icarus::SystemStatusTypeSupport_var ts_env = new Icarus::SystemStatusTypeSupportImpl();
        if (ts_env->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        CORBA::String_var type_name_imu = ts_imu->get_type_name();
        DDS::Topic_var topic_imu = participant->create_topic("IMU_Topic", type_name_imu, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        CORBA::String_var type_name_env = ts_env->get_type_name();
        DDS::Topic_var topic_env = participant->create_topic("Env_Topic", type_name_env, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::Subscriber_var sub = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::DataReader_var dr_imu = sub->create_datareader(topic_imu, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        Icarus::IMUDataDataReader_var reader_imu = Icarus::IMUDataDataReader::_narrow(dr_imu);

        DDS::DataReader_var dr_env = sub->create_datareader(topic_env, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        Icarus::SystemStatusDataReader_var reader_env = Icarus::SystemStatusDataReader::_narrow(dr_env);

        std::cout << "=== [FLIGHT CONTROLLER OpenDDS] Iniciando... Monitorizando IMU y Batería" << std::endl;

        bool emergency_landing = false;
        float current_battery = 100.0f;
        int log_counter = 0;

        Icarus::IMUData msg_imu;
        Icarus::SystemStatus msg_env;
        DDS::SampleInfo info;
        DDS::ReturnCode_t ret;

        while (true) {

            ret = reader_imu->take_next_sample(msg_imu, info);

            if (ret == DDS::RETCODE_OK && info.valid_data) {
                float x = msg_imu.accel_x;
                float y = msg_imu.accel_y;
                float z = msg_imu.accel_z;

                float total = std::sqrt((x*x) + (y*y) + (z*z));

                if (emergency_landing) {
                    if (++log_counter % 50 == 0) {
                        std::cout << "[FCC] !!! ATERRIZAJE DE EMERGENCIA !!! Altitud descendiendo..." << std::endl;
                    }
                } else {
                    float disturbance = std::abs(total - 9.81f);
                    if (disturbance > 2.0f) {
                        std::cout << "[ALERTA] TURBULENCIA DETECTADA! G: " 
                                  << std::fixed << std::setprecision(2) << total 
                                  << " -> CORRIGIENDO MOTORES" << std::endl;
                    }
                }
            }

            ret = reader_env->take_next_sample(msg_env, info);

            if (ret == DDS::RETCODE_OK && info.valid_data) {
                current_battery = msg_env.battery_level;

                std::cout << "[SISTEMA] Estado recibido -> Batería: " << std::fixed << std::setprecision(1) << current_battery 
                          << "% | GPS: " << (msg_env.gps_valid ? "OK" : "NO_FIX") 
                          << " | Lat: " << msg_env.latitude << std::endl;

                if (current_battery < 20.0f && !emergency_landing) {
                    std::cout << "[ALARMA] BATERÍA CRÍTICA (<20%). INICIANDO PROTOCOLO DE RETORNO (RTL)." << std::endl;
                    emergency_landing = true;
                }
            }

            ACE_OS::sleep(ACE_Time_Value(0, 1000)); 
        }

    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception in flight_controller:");
        return 1;
    }
    return 0;
}
