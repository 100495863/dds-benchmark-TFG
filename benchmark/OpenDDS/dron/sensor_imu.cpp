#include "dronTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/StaticIncludes.h>
#include <ace/OS_NS_unistd.h>
#include <ace/streams.h>
#include <cmath>
#include <cstdlib>
#include <ctime>

int ACE_TMAIN(int argc, ACE_TCHAR* argv[])
{
    try {
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        DDS::DomainParticipant_var participant = dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        if (!participant) return 1;

        Icarus::IMUDataTypeSupport_var ts = new Icarus::IMUDataTypeSupportImpl();
        if (ts->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        CORBA::String_var type_name = ts->get_type_name();
        DDS::Topic_var topic = participant->create_topic("IMU_Topic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::Publisher_var pub = participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::DataWriter_var dw = pub->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        Icarus::IMUDataDataWriter_var writer = Icarus::IMUDataDataWriter::_narrow(dw);
        if (!writer) return 1;

        std::cout << "=== [SENSOR IMU OpenDDS] Iniciando con Turbulencias..." << std::endl;

        Icarus::IMUData msg;
        msg.sensor_id = CORBA::string_dup("IMU_OpenDDS");

        float t = 0.0f;
        int turbulence_cooldown = 0;
        std::srand(std::time(nullptr));

        while (true) {
            t += 0.1f;
            msg.timestamp_ns = 0;

            float base_ax = 0.5f * std::sin(t);
            float base_ay = 0.5f * std::cos(t);
            float base_az = 9.81f;

            if (turbulence_cooldown > 0) {
                turbulence_cooldown--;
                float noise = ((float)std::rand()/RAND_MAX * 0.2f) - 0.1f;
                msg.accel_z = base_az + noise;
                msg.accel_x = base_ax;
                msg.accel_y = base_ay;
            } else {
                int dice = std::rand() % 200;
                if (dice == 0) {
                    float sign = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
                    float force = 3.0f + ((float)std::rand() / RAND_MAX * 3.0f);
                    float spike = force * sign;

                    msg.accel_z = base_az + spike;
                    msg.accel_x = base_ax + (spike/3.0f);
                    msg.accel_y = base_ay;

                    std::cout << ">>> [SENSOR] !! RAFAGA !!: " << spike << " G" << std::endl;
                    turbulence_cooldown = 200;
                } else {
                    float noise = ((float)std::rand()/RAND_MAX * 0.2f) - 0.1f;
                    msg.accel_z = base_az + noise;
                    msg.accel_x = base_ax;
                    msg.accel_y = base_ay;
                }
            }

            writer->write(msg, DDS::HANDLE_NIL);
            ACE_OS::sleep(ACE_Time_Value(0, 10000));
        }

    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception caught in sensor_imu:");
        return 1;
    }
    return 0;
}
