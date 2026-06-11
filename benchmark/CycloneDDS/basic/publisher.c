//
// Created by Hugob on 02/02/2026.
//

#include "dds/dds.h"
#include "benchmark_mes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main (int argc, char **argv)
{
    uint32_t payload_size = (argc > 1) ? atoi(argv[1]) : 128;

    dds_entity_t participant;
    dds_entity_t topic;
    dds_entity_t writer;
    dds_return_t rc;
    uint32_t status = 0;

    Benchmark_Msg msg;

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

    writer = dds_create_writer(participant, topic, NULL, NULL);
    if (writer < 0)
        DDS_FATAL("writer: %s\n", dds_strretcode(-writer));

    printf("=== [Publisher] Waiting for subscriber...\n");

    dds_set_status_mask(writer, DDS_PUBLICATION_MATCHED_STATUS);
    while (!(status & DDS_PUBLICATION_MATCHED_STATUS)) {
        dds_get_status_changes(writer, &status);
        dds_sleepfor(DDS_MSECS(20));
    }

    msg.payload._length = payload_size;
    msg.payload._buffer = malloc(payload_size);
    memset(msg.payload._buffer, 0xAB, payload_size);

    uint32_t seq = 0;

    printf("=== [Publisher] Sending messages (payload=%u bytes)\n", payload_size);

    while (1) {
        msg.seq = seq++;
        msg.timestamp_ns = now_ns();

        rc = dds_write(writer, &msg);
        if (rc != DDS_RETCODE_OK)
            DDS_FATAL("write: %s\n", dds_strretcode(-rc));

        dds_sleepfor(DDS_MSECS(1));
    }

    return EXIT_SUCCESS;
}
