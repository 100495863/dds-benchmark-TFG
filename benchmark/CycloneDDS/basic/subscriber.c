//
// Created by Hugob on 02/02/2026.
//

#include "dds/dds.h"
#include "benchmark_mes.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

#define MAX_SAMPLES 1

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(void)
{
    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t reader;
    dds_return_t rc;

    void *samples[MAX_SAMPLES];
    dds_sample_info_t infos[MAX_SAMPLES];

    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    if (participant < 0)
        DDS_FATAL("participant: %s\n", dds_strretcode(-participant));

    topic = dds_create_topic(
      participant,
      &Benchmark_Msg_desc,
      "BenchmarkTopic",
      NULL, NULL);
    if (topic < 0)
        DDS_FATAL("topic: %s\n", dds_strretcode(-topic));

    reader = dds_create_reader(participant, topic, NULL, NULL);
    if (reader < 0)
        DDS_FATAL("reader: %s\n", dds_strretcode(-reader));

    samples[0] = Benchmark_Msg__alloc();

    printf("=== [Subscriber] Waiting for samples...\n");

    while (1) {
        rc = dds_take(reader, samples, infos, MAX_SAMPLES, MAX_SAMPLES);
        if (rc < 0)
            DDS_FATAL("read: %s\n", dds_strretcode(-rc));

        if (rc > 0 && infos[0].valid_data) {
            Benchmark_Msg *msg = (Benchmark_Msg*)samples[0];
            uint64_t latency = now_ns() - msg->timestamp_ns;

            printf("seq=%u latency_ns=%" PRIu64 "\n", msg->seq, latency);
        }

        dds_sleepfor(DDS_MSECS(1));
    }

    return EXIT_SUCCESS;
}
