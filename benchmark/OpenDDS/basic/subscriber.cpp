#include "benchmarkC.h"
#include "benchmarkTypeSupportC.h"
#include "benchmarkTypeSupportImpl.h"

#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/SubscriberImpl.h>
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
        if (ts->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        CORBA::String_var type_name = ts->get_type_name();

        DDS::Topic_var topic =
            participant->create_topic("BenchmarkTopic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!topic) return 1;

        DDS::Subscriber_var sub =
            participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!sub) return 1;

        DDS::DataReader_var dr =
            sub->create_datareader(topic, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!dr) return 1;

        Benchmark::MsgDataReader_ptr reader = Benchmark::MsgDataReader::_narrow(dr);
        if (!reader) return 1;

        std::cout << "Subscriber started" << std::endl;

        while (true) {
            Benchmark::Msg msg;
            DDS::SampleInfo info;

            if (reader->take_next_sample(msg, info) == DDS::RETCODE_OK) {
                if (info.valid_data) {
                    unsigned long long now = ACE_OS::gethrtime();
                    unsigned long long latency = (now > msg.timestamp_ns) ? (now - msg.timestamp_ns) : 0;

                    std::cout << "seq=" << msg.seq << " latency_ns=" << latency << std::endl;
                }
            } else {
                ACE_OS::sleep(ACE_Time_Value(0, 1000));
            }
        }
    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception in subscriber:");
        return 1;
    }
    return 0;
}
