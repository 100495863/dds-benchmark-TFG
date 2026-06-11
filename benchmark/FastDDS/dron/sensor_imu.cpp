#include <chrono>
#include <thread>
#include <cmath>
#include <vector>
#include <iostream>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/topic/Topic.hpp>

#include "dronPubSubTypes.hpp"

using namespace eprosima::fastdds::dds;

static uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, PARTICIPANT_QOS_DEFAULT);

    TypeSupport type(new Icarus::IMUDataPubSubType());
    type.register_type(participant);

    Topic* topic = participant->create_topic("IMU_Topic", type.get_type_name(), TOPIC_QOS_DEFAULT);

    Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);

    DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;
    wqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
    DataWriter* writer = publisher->create_datawriter(topic, wqos);

    std::cout << "=== [SENSOR IMU FastDDS] Iniciando Vuelo con Turbulencias..." << std::endl;

    Icarus::IMUData msg;
    msg.sensor_id("IMU_Fast_1");
    
    float t = 0.0f;
    int turbulence_cooldown = 0;
    srand(time(NULL));

    while (true) {
        msg.timestamp_ns(now_ns());
        t += 0.1f;

        float base_ax = 0.5f * sinf(t);
        float base_ay = 0.5f * cosf(t);
        float base_az = 9.81f;

        if (turbulence_cooldown > 0) {
            turbulence_cooldown--;
            float noise = ((float)rand()/RAND_MAX * 0.2f) - 0.1f;
            msg.accel_z(base_az + noise);
            msg.accel_x(base_ax);
            msg.accel_y(base_ay);
        } else {
            int dice = rand() % 200;
            if (dice == 0) {
                float sign = (rand() % 2 == 0) ? 1.0f : -1.0f;
                float force = 3.0f + ((float)rand() / RAND_MAX * 3.0f);
                float spike = force * sign;

                msg.accel_z(base_az + spike);
                msg.accel_x(base_ax + (spike/3.0f));
                msg.accel_y(base_ay);

                std::cout << ">>> [SENSOR] !! RAFAGA !!: " << spike << " G" << std::endl;
                turbulence_cooldown = 200;
            } else {
                float noise = ((float)rand()/RAND_MAX * 0.2f) - 0.1f;
                msg.accel_z(base_az + noise);
                msg.accel_x(base_ax);
                msg.accel_y(base_ay);
            }
        }

        writer->write(&msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
