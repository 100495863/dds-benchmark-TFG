#include "benchmarkC.h"
#include "benchmarkTypeSupportC.h"
#include "benchmarkTypeSupportImpl.h"

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

        DDS::DomainParticipant_var participant =
            dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!participant) return 1;

        DDS::TypeSupport_var ts = new Benchmark::MsgTypeSupportImpl();

        if (ts->register_type(participant, "") != DDS::RETCODE_OK) {
            std::cerr << "Failed to register type" << std::endl;
            return 1;
        }

        CORBA::String_var type_name = ts->get_type_name();

        DDS::Topic_var topic =
            participant->create_topic("BenchmarkTopic",
                                      type_name,
                                      TOPIC_QOS_DEFAULT,
                                      0,
                                      OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!topic) return 1;

        DDS::Publisher_var pub =
            participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!pub) return 1;

        DDS::DataWriter_var dw =
            pub->create_datawriter(topic,
                                   DATAWRITER_QOS_DEFAULT,
                                   0,
                                   OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!dw) return 1;

        Benchmark::MsgDataWriter_ptr writer = Benchmark::MsgDataWriter::_narrow(dw);

        if (!writer) {
             std::cerr << "Failed to narrow DataWriter" << std::endl;
             return 1;
        }

        std::cout << "Publisher started" << std::endl;

        Benchmark::Msg msg;
        for (uint64_t i = 0; ; ++i) {
            msg.seq = i;
            msg.timestamp_ns = ACE_OS::gethrtime();

            writer->write(msg, DDS::HANDLE_NIL);

            ACE_OS::sleep(ACE_Time_Value(0, 1000));
        }

    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception caught in publisher:");
        return 1;
    }

    return 0;
}
