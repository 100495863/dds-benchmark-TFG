#include "dronTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/StaticIncludes.h>
#include <ace/OS_NS_unistd.h>
#include <ace/streams.h>

int ACE_TMAIN(int argc, ACE_TCHAR* argv[])
{
    try {
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        DDS::DomainParticipant_var participant = dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        if (!participant) return 1;

        Icarus::SystemStatusTypeSupport_var ts = new Icarus::SystemStatusTypeSupportImpl();
        if (ts->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        CORBA::String_var type_name = ts->get_type_name();
        DDS::Topic_var topic = participant->create_topic("Env_Topic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::Publisher_var pub = participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::DataWriter_var dw = pub->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        Icarus::SystemStatusDataWriter_var writer = Icarus::SystemStatusDataWriter::_narrow(dw);
        if (!writer) return 1;

        std::cout << "=== [SENSOR ENV OpenDDS] Iniciando GPS/Bateria (1Hz)..." << std::endl;

        Icarus::SystemStatus msg;
        msg.system_id = CORBA::string_dup("PowerUnit_OpenDDS");
        msg.battery_level = 100.0f;
        msg.latitude = 40.4168;
        msg.longitude = -3.7038;
        msg.gps_valid = true;

        while (true) {
            msg.timestamp_ns = 0;

            msg.battery_level -= 0.5f;
            if(msg.battery_level < 0) msg.battery_level = 100.0f;

            msg.longitude += 0.0001;

            msg.gps_valid = (msg.battery_level > 10.0f);

            writer->write(msg, DDS::HANDLE_NIL);
            ACE_OS::sleep(ACE_Time_Value(1, 0));
        }

    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception caught in sensor_env:");
        return 1;
    }
    return 0;
}
