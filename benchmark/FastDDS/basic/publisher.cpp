#include <chrono>
#include <atomic>
#include <thread>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/topic/Topic.hpp>


#include "benchmarkPubSubTypes.hpp"


using namespace eprosima::fastdds::dds;


static uint64_t now_ns()
{
return std::chrono::duration_cast<std::chrono::nanoseconds>(
std::chrono::steady_clock::now().time_since_epoch()).count();
}


class PubListener : public DataWriterListener
{
public:
std::atomic<int> matched{0};


void on_publication_matched(DataWriter*, const PublicationMatchedStatus& info) override
{
if (info.current_count_change == 1)
{
matched++;
std::cout << "Publisher matched" << std::endl;
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


Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);


DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;
wqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
wqos.history().depth = 1;


PubListener listener;


DataWriter* writer = publisher->create_datawriter(topic, wqos, &listener);


std::cout << "Waiting for subscriber..." << std::endl;


while (listener.matched.load() == 0)
std::this_thread::sleep_for(std::chrono::milliseconds(20));


Benchmark::Msg msg;
msg.seq(0);


std::cout << "Sending messages..." << std::endl;


while (true)
{
msg.seq(msg.seq() + 1);
msg.timestamp_ns(now_ns());


writer->write(&msg);


std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
}
