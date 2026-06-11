#include <chrono>
#include <thread>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/topic/Topic.hpp>


#include "benchmarkPubSubTypes.hpp"


using namespace eprosima::fastdds::dds;


static uint64_t now_ns()
{
return std::chrono::duration_cast<std::chrono::nanoseconds>(
std::chrono::steady_clock::now().time_since_epoch()).count();
}


class SubListener : public DataReaderListener
{
public:
void on_data_available(DataReader* reader) override
{
SampleInfo info;
Benchmark::Msg msg;


while (reader->take_next_sample(&msg, &info) == RETCODE_OK)
{
if (info.valid_data)
{
uint64_t latency = now_ns() - msg.timestamp_ns();


std::cout << "seq=" << msg.seq()
<< " latency_ns=" << latency
<< std::endl;
}
}
}
};


int main()
{
DomainParticipant* participant =
DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);


TypeSupport type(new Benchmark::MsgPubSubType());
type.register_type(participant);


Topic* topic = participant->create_topic("latency_topic", type.get_type_name(), TOPIC_QOS_DEFAULT);


Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);


DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
rqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
rqos.history().depth = 1;


SubListener listener;


subscriber->create_datareader(topic, rqos, &listener);


std::cout << "Waiting for data..." << std::endl;


while (true)
std::this_thread::sleep_for(std::chrono::seconds(1));
}
