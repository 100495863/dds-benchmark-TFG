#include <chrono>
#include <thread>
#include <iostream>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/topic/Topic.hpp>

#include "dronPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);
    TypeSupport type(new Icarus::SystemStatusPubSubType());
    type.register_type(participant);
    Topic* topic = participant->create_topic("Env_Topic", type.get_type_name(), TOPIC_QOS_DEFAULT);
    Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
    DataWriter* writer = publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);

    std::cout << "=== [SENSOR ENV FastDDS] Iniciando GPS/Bateria (1Hz)..." << std::endl;

    Icarus::SystemStatus msg;
    msg.system_id("PowerUnit_1");
    msg.battery_level(100.0f);
    msg.latitude(40.4168);
    msg.longitude(-3.7038);
    msg.gps_valid(true);

    while (true) {
        msg.timestamp_ns(now_ns());
        float current_bat = msg.battery_level();
        current_bat -= 0.5f;
        if(current_bat < 0) current_bat = 100.0f;
        msg.battery_level(current_bat);

        msg.longitude(msg.longitude() + 0.0001);

        msg.gps_valid(current_bat > 10.0f); 

        writer->write(&msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return 0;
}
