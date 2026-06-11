#include <dds/DdsDcpsInfrastructureC.h>
#include <dds/DdsDcpsPublicationC.h>
#include <dds/DdsDcpsSubscriptionC.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/Service_Participant.h>

#include "multimediaTypeSupportImpl.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <mutex>

using namespace Streaming;

// Medición en nanosegundos
uint64_t now_ns() {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

// Variables compartidas
std::mutex stats_mutex;
double total_video_mb = 0.0;
uint32_t video_lost_frames = 0;

// NUEVAS VARIABLES PARA LATENCIA
double total_video_latency_ms = 0.0;
uint32_t video_frames_received = 0;

double max_jitter_ms = 0.0;
uint32_t audio_lost_frames = 0;

int main(int argc, char *argv[]) {
    try {
        // Inicializar factoría con args de CORBA
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        
        bool is_publisher = true;
        for(int i=0; i<argc; ++i) {
            if(std::string(argv[i]) == "sub") is_publisher = false;
        }

        DDS::DomainParticipant_var participant = dpf->create_participant(
            0, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // Registro de Tipos (Estilo OpenDDS)
        VideoFrameTypeSupport_var video_ts = new VideoFrameTypeSupportImpl();
        video_ts->register_type(participant, "");
        CORBA::String_var video_type_name = video_ts->get_type_name();

        AudioFrameTypeSupport_var audio_ts = new AudioFrameTypeSupportImpl();
        audio_ts->register_type(participant, "");
        CORBA::String_var audio_type_name = audio_ts->get_type_name();

        DDS::Topic_var video_topic = participant->create_topic(
            "VideoTopic", video_type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::Topic_var audio_topic = participant->create_topic(
            "AudioTopic", audio_type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (is_publisher) {
            DDS::Publisher_var pub = participant->create_publisher(
                PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

            // _narrow de los Writers
            DDS::DataWriter_var v_dw_base = pub->create_datawriter(video_topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
            VideoFrameDataWriter_var video_writer = VideoFrameDataWriter::_narrow(v_dw_base);

            DDS::DataWriter_var a_dw_base = pub->create_datawriter(audio_topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
            AudioFrameDataWriter_var audio_writer = AudioFrameDataWriter::_narrow(a_dw_base);

            std::cout << "[PUBLISHER] Iniciando Video (SATURACIÓN MÁXIMA) y Audio (50Hz) en OpenDDS...\n";

            std::thread video_thread([&]() {
                VideoFrame v_msg;
                v_msg.width = 1920; v_msg.height = 1080;
                v_msg.pixels.length(2 * 1024 * 1024); // 2 MB
                for(unsigned int i=0; i<v_msg.pixels.length(); ++i) v_msg.pixels[i] = 0xAA;

                uint32_t id = 0;

                // Esperar de forma inteligente al suscriptor
                DDS::PublicationMatchedStatus match_status;
                std::cout << "Esperando a que se conecte el suscriptor de vídeo...\n";
                do {
                    video_writer->get_publication_matched_status(match_status);
                    if (match_status.current_count == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                } while (match_status.current_count == 0);
                std::cout << "¡Suscriptor de vídeo detectado! Arrancando transmisión...\n";

                while(true) {
                    v_msg.frame_id = ++id;
                    v_msg.timestamp = now_ns();
                    video_writer->write(v_msg, DDS::HANDLE_NIL);
                    
                    // LÍNEA COMENTADA PARA PROVOCAR FLOODING Y ESTRÉS DE RED
                    // std::this_thread::sleep_for(std::chrono::milliseconds(33));
                }
            });

            std::thread audio_thread([&]() {
                AudioFrame a_msg;
                a_msg.sample_rate = 48000;
                a_msg.samples.length(1024); // 1 KB
                for(unsigned int i=0; i<a_msg.samples.length(); ++i) a_msg.samples[i] = 0xBB;

                uint32_t id = 0;

                // Esperar de forma inteligente al suscriptor
                DDS::PublicationMatchedStatus match_status;
                std::cout << "Esperando a que se conecte el suscriptor de audio...\n";
                do {
                    audio_writer->get_publication_matched_status(match_status);
                    if (match_status.current_count == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                } while (match_status.current_count == 0);
                std::cout << "¡Suscriptor de audio detectado! Arrancando transmisión...\n";

                while(true) {
                    a_msg.frame_id = ++id;
                    a_msg.timestamp = now_ns();
                    audio_writer->write(a_msg, DDS::HANDLE_NIL);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            });

            video_thread.join();
            audio_thread.join();

        } else {
            DDS::Subscriber_var sub = participant->create_subscriber(
                SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

            // _narrow de los Readers
            DDS::DataReader_var v_dr_base = sub->create_datareader(video_topic, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
            VideoFrameDataReader_var video_reader = VideoFrameDataReader::_narrow(v_dr_base);

            DDS::DataReader_var a_dr_base = sub->create_datareader(audio_topic, DATAREADER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
            AudioFrameDataReader_var audio_reader = AudioFrameDataReader::_narrow(a_dr_base);

            std::cout << "[SUBSCRIBER] Escuchando Streaming en OpenDDS...\n";

            std::thread video_thread([&]() {
                VideoFrame msg;
                DDS::SampleInfo info;
                uint32_t last_id = 0;
                while(true) {
                    if (video_reader->take_next_sample(msg, info) == DDS::RETCODE_OK && info.valid_data) {
                        uint64_t now = now_ns();
                        double latency = (now - msg.timestamp) / 1000000.0; // a ms
                        
                        std::lock_guard<std::mutex> lock(stats_mutex);
                        total_video_mb += msg.pixels.length() / (1024.0 * 1024.0);
                        total_video_latency_ms += latency;
                        video_frames_received++;
                        
                        if (last_id != 0 && msg.frame_id > last_id + 1) video_lost_frames += (msg.frame_id - last_id - 1);
                        last_id = msg.frame_id;
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            });

            std::thread audio_thread([&]() {
                AudioFrame msg;
                DDS::SampleInfo info;
                uint64_t last_arrival = 0;
                uint32_t last_id = 0;
                while(true) {
                    if (audio_reader->take_next_sample(msg, info) == DDS::RETCODE_OK && info.valid_data) {
                        uint64_t now = now_ns();
                        std::lock_guard<std::mutex> lock(stats_mutex);
                        
                        if (last_arrival != 0) {
                            double delta_ms = (now - last_arrival) / 1000000.0;
                            double jitter = std::abs(delta_ms - 20.0);
                            if (jitter > max_jitter_ms) max_jitter_ms = jitter;
                        }
                        last_arrival = now;
                        
                        if (last_id != 0 && msg.frame_id > last_id + 1) audio_lost_frames += (msg.frame_id - last_id - 1);
                        last_id = msg.frame_id;
                    } else {
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                }
            });

            while(true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::lock_guard<std::mutex> lock(stats_mutex);
                
                double avg_latency = (video_frames_received > 0) ? (total_video_latency_ms / video_frames_received) : 0.0;
                
                std::cout << "------------------------------------------\n";
                std::cout << "[VÍDEO] Throughput: " << total_video_mb << " MB/s | Perdidos: " << video_lost_frames << " | Latencia: " << avg_latency << " ms\n";
                std::cout << "[AUDIO] Max Jitter: " << max_jitter_ms << " ms  | Perdidos: " << audio_lost_frames << "\n";
                
                total_video_mb = 0.0;
                total_video_latency_ms = 0.0;
                video_frames_received = 0;
                max_jitter_ms = 0.0;
            }
        }
    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Excepción en CORBA:");
    }
    return 0;
}
