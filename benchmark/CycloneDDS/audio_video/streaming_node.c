#include "dds/dds.h"
#include "multimedia.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

// Variables globales para métricas del Suscriptor (protegidas por mutex)
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
double total_video_mb = 0.0;
uint32_t video_lost_frames = 0;

// NUEVAS VARIABLES PARA LATENCIA
double total_video_latency_ms = 0.0;
uint32_t video_frames_received = 0;

double max_jitter_ms = 0.0;
uint32_t audio_lost_frames = 0;

// Función auxiliar para medir tiempo en nanosegundos
uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// ---------------------------------------------------------
// HILOS DEL PUBLICADOR
// ---------------------------------------------------------
void* video_publisher_thread(void* arg) {
    dds_entity_t writer = *(dds_entity_t*)arg;
    Streaming_VideoFrame *v_msg = Streaming_VideoFrame__alloc();
    v_msg->width = 1920; v_msg->height = 1080;
    
    // Gestión manual de memoria en C para la secuencia (2 MB)
    v_msg->pixels._length = 2 * 1024 * 1024;
    v_msg->pixels._maximum = 2 * 1024 * 1024;
    v_msg->pixels._buffer = dds_alloc(v_msg->pixels._maximum);
    v_msg->pixels._release = true;
    memset(v_msg->pixels._buffer, 0xAA, v_msg->pixels._length);

    uint32_t id = 0;

    // Esperar de forma inteligente al suscriptor
    dds_publication_matched_status_t match_status;
    printf("Esperando a que se conecte el suscriptor de vídeo...\n");
    do {
        dds_get_publication_matched_status(writer, &match_status);
        if (match_status.current_count == 0) {
            usleep(500000); // Esperar 500ms antes de volver a comprobar
        }
    } while (match_status.current_count == 0);
    printf("¡Suscriptor de vídeo detectado! Arrancando transmisión a máxima velocidad...\n");

    while (1) {
        v_msg->frame_id = ++id;
        v_msg->timestamp = now_ns();
        dds_write(writer, v_msg);
        
        // LÍNEA COMENTADA PARA PROVOCAR FLOODING Y ESTRÉS DE RED
        // usleep(33000); // ~30 FPS
    }
    return NULL;
}

void* audio_publisher_thread(void* arg) {
    dds_entity_t writer = *(dds_entity_t*)arg;
    Streaming_AudioFrame *a_msg = Streaming_AudioFrame__alloc();
    a_msg->sample_rate = 48000;
    
    // 1 KB de Audio
    a_msg->samples._length = 1024;
    a_msg->samples._maximum = 1024;
    a_msg->samples._buffer = dds_alloc(a_msg->samples._maximum);
    a_msg->samples._release = true;
    memset(a_msg->samples._buffer, 0xBB, a_msg->samples._length);

    uint32_t id = 0;

    // Esperar de forma inteligente al suscriptor
    dds_publication_matched_status_t match_status;
    printf("Esperando a que se conecte el suscriptor de audio...\n");
    do {
        dds_get_publication_matched_status(writer, &match_status);
        if (match_status.current_count == 0) {
            usleep(500000); // Esperar 500ms antes de volver a comprobar
        }
    } while (match_status.current_count == 0);
    printf("¡Suscriptor de audio detectado! Arrancando transmisión...\n");

    while (1) {
        a_msg->frame_id = ++id;
        a_msg->timestamp = now_ns();
        dds_write(writer, a_msg);
        usleep(20000); // 50 Hz
    }
    return NULL;
}

// ---------------------------------------------------------
// HILOS DEL SUSCRIPTOR
// ---------------------------------------------------------
void* video_subscriber_thread(void* arg) {
    dds_entity_t reader = *(dds_entity_t*)arg;
    void *samples[1];
    dds_sample_info_t infos[1];
    samples[0] = Streaming_VideoFrame__alloc();
    uint32_t last_id = 0;

    while (1) {
        int n = dds_take(reader, samples, infos, 1, 1);
        if (n > 0 && infos[0].valid_data) {
            Streaming_VideoFrame *msg = (Streaming_VideoFrame*)samples[0];
            uint64_t now = now_ns();
            double latency = (now - msg->timestamp) / 1000000.0; // a ms
            
            pthread_mutex_lock(&stats_mutex);
            total_video_mb += (double)(msg->pixels._length) / (1024.0 * 1024.0);
            total_video_latency_ms += latency;
            video_frames_received++;

            if (last_id != 0 && msg->frame_id > last_id + 1) {
                video_lost_frames += (msg->frame_id - last_id - 1);
            }
            pthread_mutex_unlock(&stats_mutex);
            last_id = msg->frame_id;
        } else {
            usleep(1000); // No saturar CPU si no hay datos
        }
    }
    return NULL;
}

void* audio_subscriber_thread(void* arg) {
    dds_entity_t reader = *(dds_entity_t*)arg;
    void *samples[1];
    dds_sample_info_t infos[1];
    samples[0] = Streaming_AudioFrame__alloc();
    
    uint64_t last_arrival = 0;
    uint32_t last_id = 0;

    while (1) {
        int n = dds_take(reader, samples, infos, 1, 1);
        if (n > 0 && infos[0].valid_data) {
            Streaming_AudioFrame *msg = (Streaming_AudioFrame*)samples[0];
            uint64_t now = now_ns();
            
            pthread_mutex_lock(&stats_mutex);
            if (last_arrival != 0) {
                double delta_ms = (now - last_arrival) / 1000000.0;
                double jitter = fabs(delta_ms - 20.0);
                if (jitter > max_jitter_ms) max_jitter_ms = jitter;
            }
            last_arrival = now;

            if (last_id != 0 && msg->frame_id > last_id + 1) {
                audio_lost_frames += (msg->frame_id - last_id - 1);
            }
            pthread_mutex_unlock(&stats_mutex);
            last_id = msg->frame_id;
        } else {
            usleep(500); // No saturar CPU
        }
    }
    return NULL;
}

// ---------------------------------------------------------
// FUNCIÓN PRINCIPAL
// ---------------------------------------------------------
int main(int argc, char **argv) {
    int is_publisher = 1;
    if (argc > 1 && strcmp(argv[1], "sub") == 0) {
        is_publisher = 0;
    }

    dds_entity_t participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);

    dds_entity_t video_topic = dds_create_topic(participant, &Streaming_VideoFrame_desc, "VideoTopic", NULL, NULL);
    dds_entity_t audio_topic = dds_create_topic(participant, &Streaming_AudioFrame_desc, "AudioTopic", NULL, NULL);

    if (is_publisher) {
        printf("[PUBLISHER] Iniciando hilos de Video (SATURACIÓN MÁXIMA) y Audio (50Hz) en CycloneDDS...\n");
        
        dds_entity_t video_writer = dds_create_writer(participant, video_topic, NULL, NULL);
        dds_entity_t audio_writer = dds_create_writer(participant, audio_topic, NULL, NULL);

        pthread_t v_thread, a_thread;
        pthread_create(&v_thread, NULL, video_publisher_thread, &video_writer);
        pthread_create(&a_thread, NULL, audio_publisher_thread, &audio_writer);

        pthread_join(v_thread, NULL);
        pthread_join(a_thread, NULL);

    } else {
        printf("[SUBSCRIBER] Escuchando Streaming Multimedia en CycloneDDS...\n");
        
        dds_entity_t video_reader = dds_create_reader(participant, video_topic, NULL, NULL);
        dds_entity_t audio_reader = dds_create_reader(participant, audio_topic, NULL, NULL);

        pthread_t v_thread, a_thread;
        pthread_create(&v_thread, NULL, video_subscriber_thread, &video_reader);
        pthread_create(&a_thread, NULL, audio_subscriber_thread, &audio_reader);

        // Bucle de impresión de estadísticas cada 1 segundo
        while (1) {
            sleep(1);
            pthread_mutex_lock(&stats_mutex);
            
            double avg_latency = (video_frames_received > 0) ? (total_video_latency_ms / video_frames_received) : 0.0;
            
            printf("------------------------------------------\n");
            printf("[VÍDEO] Throughput: %.2f MB/s | Perdidos: %u | Latencia: %.2f ms\n", total_video_mb, video_lost_frames, avg_latency);
            printf("[AUDIO] Max Jitter: %.2f ms  | Perdidos: %u\n", max_jitter_ms, audio_lost_frames);
            
            // Reiniciar contadores del segundo
            total_video_mb = 0.0;
            total_video_latency_ms = 0.0;
            video_frames_received = 0;
            max_jitter_ms = 0.0;
            
            pthread_mutex_unlock(&stats_mutex);
        }
    }

    return 0;
}
