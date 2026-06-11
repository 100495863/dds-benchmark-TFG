#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>

#include "multimediaPubSubTypes.hpp"

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <atomic>
#include <cmath>
#include <mutex>

using namespace eprosima::fastdds::dds;

// Función auxiliar para medir tiempo en nanosegundos (reloj constante)
inline uint64_t now_ns() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

// Mutex para proteger la lectura/escritura de estadísticas
std::mutex stats_mutex;

// ---------------------------------------------------------
// LISTENERS PARA EL SUSCRIPTOR
// ---------------------------------------------------------
class VideoListener : public DataReaderListener {
public:
    uint64_t total_bytes = 0;
    uint32_t last_id = 0;
    uint32_t lost_frames = 0;
    
    // NUEVAS VARIABLES PARA LATENCIA
    double total_latency_ms = 0.0;
    uint32_t frames_received = 0;

    void on_data_available(DataReader* reader) override {
        Streaming::VideoFrame msg;
        SampleInfo info;
        if (reader->take_next_sample(&msg, &info) == RETCODE_OK && info.valid_data) {
            uint64_t now = now_ns();
            double latency = (now - msg.timestamp()) / 1000000.0; // Convertir a ms

            std::lock_guard<std::mutex> lock(stats_mutex);
            total_bytes += msg.pixels().size();
            total_latency_ms += latency;
            frames_received++;
            
            // Detección de pérdida de frames
            if (last_id != 0 && msg.frame_id() > last_id + 1) {
                lost_frames += (msg.frame_id() - last_id - 1);
            }
            last_id = msg.frame_id();
        }
    }
};

class AudioListener : public DataReaderListener {
public:
    uint64_t last_arrival = 0;
    uint32_t last_id = 0;
    uint32_t lost_frames = 0;
    double max_jitter_ms = 0.0;

    void on_data_available(DataReader* reader) override {
        Streaming::AudioFrame msg;
        SampleInfo info;
        if (reader->take_next_sample(&msg, &info) == RETCODE_OK && info.valid_data) {
            uint64_t now = now_ns();
            
            std::lock_guard<std::mutex> lock(stats_mutex);
            // Cálculo del Jitter (Esperamos 1 paquete cada 20ms exactos)
            if (last_arrival != 0) {
                double delta_ms = (now - last_arrival) / 1000000.0;
                double jitter = std::abs(delta_ms - 20.0);
                if (jitter > max_jitter_ms) max_jitter_ms = jitter;
            }
            last_arrival = now;

            // Detección de pérdida de frames
            if (last_id != 0 && msg.frame_id() > last_id + 1) {
                lost_frames += (msg.frame_id() - last_id - 1);
            }
            last_id = msg.frame_id();
        }
    }
};

// ---------------------------------------------------------
// FUNCIÓN PRINCIPAL
// ---------------------------------------------------------
int main(int argc, char** argv) {
    bool is_publisher = true;
    if (argc > 1 && std::string(argv[1]) == "sub") {
        is_publisher = false;
    }

    // 1. Crear Participante
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    pqos.name("StreamingNode");
    DomainParticipant* participant = DomainParticipantFactory::get_instance()->create_participant(0, pqos);

    // 2. Registrar Tipos de Datos (Video y Audio)
    TypeSupport video_type(new Streaming::VideoFramePubSubType());
    TypeSupport audio_type(new Streaming::AudioFramePubSubType());
    video_type.register_type(participant);
    audio_type.register_type(participant);

    // 3. Crear Tópicos
    Topic* video_topic = participant->create_topic("VideoTopic", video_type.get_type_name(), TOPIC_QOS_DEFAULT);
    Topic* audio_topic = participant->create_topic("AudioTopic", audio_type.get_type_name(), TOPIC_QOS_DEFAULT);

    if (is_publisher) {
        Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);

        // QOS VÍDEO: Asíncrono para que los paquetes gigantes no bloqueen la red
        DataWriterQos video_wqos = DATAWRITER_QOS_DEFAULT;
        video_wqos.publish_mode().kind = ASYNCHRONOUS_PUBLISH_MODE;
        DataWriter* video_writer = publisher->create_datawriter(video_topic, video_wqos);

        // QOS AUDIO: Síncrono por defecto (sale inmediatamente)
        DataWriter* audio_writer = publisher->create_datawriter(audio_topic, DATAWRITER_QOS_DEFAULT);

        std::cout << "[PUBLISHER] Iniciando hilos de Video (SATURACIÓN MÁXIMA) y Audio (50Hz)..." << std::endl;

        // HILO DE VÍDEO: ~2 MB a saturación máxima (sin retardo)
        std::thread video_thread([&]() {
            Streaming::VideoFrame v_msg;
            v_msg.width(1920); v_msg.height(1080);
            v_msg.pixels().resize(2 * 1024 * 1024, 0xAA); // 2 MB
            uint32_t id = 0;

            // Esperar de forma inteligente al suscriptor
            eprosima::fastdds::dds::PublicationMatchedStatus match_status;
            std::cout << "Esperando a que se conecte el suscriptor de vídeo...\n";
            do {
                video_writer->get_publication_matched_status(match_status);
                if (match_status.current_count == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } while (match_status.current_count == 0);
            std::cout << "¡Suscriptor de vídeo detectado! Arrancando transmisión...\n";

            while (true) {
                v_msg.frame_id(++id);
                v_msg.timestamp(now_ns());
                video_writer->write(&v_msg);
                
                // LÍNEA COMENTADA PARA PROVOCAR FLOODING Y ESTRÉS DE RED
                // std::this_thread::sleep_for(std::chrono::milliseconds(33)); 
            }
        });

        // HILO DE AUDIO: ~1 KB a 50 Hz
        std::thread audio_thread([&]() {
            Streaming::AudioFrame a_msg;
            a_msg.sample_rate(48000);
            a_msg.samples().resize(1024, 0xBB); // 1 KB
            uint32_t id = 0;

            // Esperar de forma inteligente al suscriptor
            eprosima::fastdds::dds::PublicationMatchedStatus match_status;
            std::cout << "Esperando a que se conecte el suscriptor de audio...\n";
            do {
                audio_writer->get_publication_matched_status(match_status);
                if (match_status.current_count == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } while (match_status.current_count == 0);
            std::cout << "¡Suscriptor de audio detectado! Arrancando transmisión...\n";

            while (true) {
                a_msg.frame_id(++id);
                a_msg.timestamp(now_ns());
                audio_writer->write(&a_msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 50 Hz
            }
        });

        video_thread.join();
        audio_thread.join();

    } else {
        Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);

        VideoListener video_listener;
        AudioListener audio_listener;

        subscriber->create_datareader(video_topic, DATAREADER_QOS_DEFAULT, &video_listener);
        subscriber->create_datareader(audio_topic, DATAREADER_QOS_DEFAULT, &audio_listener);

        std::cout << "[SUBSCRIBER] Escuchando Streaming Multimedia..." << std::endl;

        // Bucle de impresión de estadísticas cada 1 segundo
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            std::lock_guard<std::mutex> lock(stats_mutex);
            
            double mbps = video_listener.total_bytes / (1024.0 * 1024.0);
            double avg_latency = (video_listener.frames_received > 0) ? 
                                 (video_listener.total_latency_ms / video_listener.frames_received) : 0.0;

            std::cout << "------------------------------------------\n";
            std::cout << "[VÍDEO] Throughput: " << mbps << " MB/s | Perdidos: " << video_listener.lost_frames << " | Latencia: " << avg_latency << " ms\n";
            std::cout << "[AUDIO] Max Jitter: " << audio_listener.max_jitter_ms << " ms  | Perdidos: " << audio_listener.lost_frames << "\n";
            
            // Reiniciar contadores del segundo
            video_listener.total_bytes = 0;
            video_listener.total_latency_ms = 0.0;
            video_listener.frames_received = 0;
            audio_listener.max_jitter_ms = 0.0;
        }
    }

    return 0;
}
