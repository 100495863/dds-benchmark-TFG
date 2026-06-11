// Copyright 2021 Proyectos y Sistemas de Mantenimiento SL (eProsima).
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file Monitor.cpp
 */

#include <chrono>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>


#include <fastdds_statistics_backend/listener/DomainListener.hpp>
#include <fastdds_statistics_backend/StatisticsBackend.hpp>
#include <fastdds_statistics_backend/types/EntityId.hpp>
#include <fastdds_statistics_backend/types/types.hpp>
#include <fastdds_statistics_backend/types/utils.hpp>

#include "monitor.hpp"

using namespace eprosima::statistics_backend;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

std::atomic<bool> Monitor::stop_(false);
std::mutex Monitor::terminate_cv_mtx_;
std::condition_variable Monitor::terminate_cv_;


double sum = 0.0;
double sum_sq = 0.0;
double min_val = std::numeric_limits<double>::max();
double max_val = std::numeric_limits<double>::lowest();
size_t count = 0;


double sum_t = 0.0;
double sum_sq_t = 0.0;
double min_val_t = std::numeric_limits<double>::max();
double max_val_t = std::numeric_limits<double>::lowest();
size_t count_t = 0;

struct Options
{
    uint32_t domain      = 0;
    uint16_t bins        = 1;
    uint32_t interval_s  = 5;
    std::string topic    = "SecureMulticastTopic";
    std::string dump_prefix;   // empty = disabled
    bool reset           = true;
};



Monitor::Monitor()
{
}

Monitor::~Monitor()
{
    StatisticsBackend::stop_monitor(monitor_id_);
}

bool Monitor::is_stopped()
{
    return stop_;
}

void Monitor::stop()
{
    stop_ = true;
    terminate_cv_.notify_all();
}

bool Monitor::init(
        uint32_t domain,
        uint16_t n_bins,
        uint32_t t_interval,
        std::string topic_name, 
        std::string dump_file /* = "" */,
        bool reset /* = false */)
{
    n_bins_ = n_bins;
    t_interval_ = t_interval;
    monitor_id_ = StatisticsBackend::init_monitor(domain);
    topic_name_ = std::move(topic_name); 
    dump_file_ = std::move(dump_file);
    reset_ = reset;

    if (!monitor_id_.is_valid())
    {
        std::cout << "Error creating monitor" << std::endl;
        return 1;
    }

    StatisticsBackend::set_physical_listener(&physical_listener_);

    return true;
}

void Monitor::run()
{
    stop_ = false;
    std::cout << "Monitor running. Please press CTRL+C to stop the Monitor at any time." << std::endl;
    signal(SIGINT, [](int signum)
            {
                std::cout << "SIGINT received, stopping Monitor execution." << std::endl;
                static_cast<void>(signum); Monitor::stop();
            });

    while (!is_stopped())
    {
        std::unique_lock<std::mutex> lck(terminate_cv_mtx_);
        terminate_cv_.wait_for(lck, std::chrono::seconds(t_interval_), []
                {
                    return is_stopped();
                });
        std::cout << std::endl;
        get_fastdds_latency_mean();
        get_publication_throughput_mean();

        // Dump data to file and THEN remove the inactive entities. This means inactive entities would appear in this dump file.
        if (!dump_file_.empty())
        {
            dump_in_file();
        }

        if (reset_)
        {
            clear_inactive_entities();
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Removing inactive entities from Statistics Backend." << std::endl;
        }
    }
}

void Monitor::dump_in_file()
{
    // Get current timestamp
    auto t = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif // ifdef _WIN32
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    std::string current_time = oss.str();

    // Get file name
    std::string complete_file_name = dump_file_ + "_" + current_time + ".json";
    std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Dumping info in file " << complete_file_name << std::endl;

    // Bump to get json
    auto dump = StatisticsBackend::dump_database(reset_);

    // Store it in json file
    std::ofstream file(complete_file_name);
    // Pretty print json
    file << std::setw(4) << dump << std::endl;
}

void Monitor::clear_inactive_entities()
{
    StatisticsBackend::clear_inactive_entities();
}

/***************************************************************
* Implementation of the functions to collect the data.
***************************************************************/

std::vector<StatisticsData> Monitor::get_fastdds_latency_mean()
{
    std::vector<StatisticsData> latency_data{};

    std::vector<EntityId> topics = StatisticsBackend::get_entities(EntityKind::TOPIC);
    EntityId helloworld_topic_id = -1;
    Info topic_info;
    for (auto topic_id : topics)
    {
        topic_info = StatisticsBackend::get_info(topic_id);
        if (topic_info[NAME_TAG] ==  topic_name_ )
        {
            helloworld_topic_id = topic_id;
        }
    }

    if (helloworld_topic_id < 0)
    {
        return latency_data;
    }

    /* Get the DataWriters and DataReaders in a Topic */
    std::vector<EntityId> topic_datawriters = StatisticsBackend::get_entities(
        EntityKind::DATAWRITER,
        helloworld_topic_id);
    std::vector<EntityId> topic_datareaders = StatisticsBackend::get_entities(
        EntityKind::DATAREADER,
        helloworld_topic_id);

    /* Get the current time */
    auto now_time = now();

    /*
     * Get the median of the FASTDDS_LATENCY of the last 10 minutes
     * between the DataWriters and DataReaders publishing under and subscribed to the HelloWorld topic.
     */
    latency_data = StatisticsBackend::get_data(
        DataKind::FASTDDS_LATENCY,                                   // DataKind
        topic_datawriters,                                           // Source entities
        topic_datareaders,                                           // Target entities
        n_bins_,                                                     // Number of bins
        now_time - std::chrono::seconds(t_interval_),                     // t_from
        now_time,                                                         // t_to
        StatisticKind::MEAN);                                        // Statistic

    for (auto latency : latency_data)
    {
        if (latency.second > 0) {
            double val = latency.second / 1000.0; // convert to microseconds
            sum += val;
            sum_sq += val * val;
            min_val = std::min(min_val, val);
            max_val = std::max(max_val, val);
            ++count;
        }
    }

    if (count > 0)
    {
        double mean = sum / count;
        double variance = (sum_sq / count) - (mean * mean);
        double stddev = std::sqrt(variance);

        std::cout << "---- Aggregated latency stats ----" << std::endl;
        std::cout << "Count: " << count << std::endl;
        std::cout << "Min:   " << min_val << " μs" << std::endl;
        std::cout << "Max:   " << max_val << " μs" << std::endl;
        std::cout << "Mean:  " << mean << " μs" << std::endl;
        std::cout << "Stddev:" << stddev << " μs" << std::endl;
        std::cout << "----------------------------------" << std::endl;
    }

    return latency_data;
}

std::vector<StatisticsData> Monitor::get_publication_throughput_mean()
{
    std::vector<StatisticsData> publication_throughput_data{};

    std::vector<EntityId> participants = StatisticsBackend::get_entities(EntityKind::PARTICIPANT);
    EntityId participant_id = -1;
    Info participant_info;
    for (auto participant : participants)
    {
        participant_info = StatisticsBackend::get_info(participant);
        if (participant_info[NAME_TAG] == "secure_participant" && participant_info[ALIVE_TAG])
        {
            participant_id = participant;
            break;
        }
    }

    if (participant_id < 0)
    {
        return publication_throughput_data;
    }

    /* Get the DataWriters and DataReaders in a Topic */
    std::vector<EntityId> topic_datawriters = StatisticsBackend::get_entities(
        EntityKind::DATAWRITER,
        participant_id);

    /* Get the current time */
    auto now_time = now();

    /*
     *
     */
    publication_throughput_data = StatisticsBackend::get_data(
        DataKind::PUBLICATION_THROUGHPUT,                            // DataKind
        topic_datawriters,                                           // Source entities
        n_bins_,                                                     // Number of bins
        now_time - std::chrono::seconds(t_interval_),                     // t_from
        now_time,                                                         // t_to
        StatisticKind::MEAN);                                        // Statistic

    for (auto publication_throughput : publication_throughput_data)
    {
        if (publication_throughput.second > 0) {
            double val = publication_throughput.second; // in B/s

            sum_t += val;
            sum_sq_t += val * val;
            min_val_t = std::min(min_val_t, val);
            max_val_t = std::max(max_val_t, val);
            ++count_t;
         }
    }

    if (count_t > 0)
    {
        double mean = sum_t / count_t;
        double variance_t = (sum_sq_t / count_t) - (mean * mean);
        double stddev_t = std::sqrt(variance_t);

        std::cout << "---- Aggregated publication throughput stats ----" << std::endl;
        std::cout << "Count: " << count_t << std::endl;
        std::cout << "Min:   " << min_val_t << " B/s" << std::endl;
        std::cout << "Max:   " << max_val_t << " B/s" << std::endl;
        std::cout << "Mean:  " << mean << " B/s" << std::endl;
        std::cout << "Stddev:" << stddev_t << " B/s" << std::endl;
        std::cout << "-----------------------------------------------" << std::endl;
    }

    return publication_throughput_data;
}

/***************************************************************
* Monitor Listener callbacks implementation
***************************************************************/
void Monitor::Listener::on_participant_discovery(
        EntityId domain_id,
        EntityId participant_id,
        const DomainListener::Status& status)
{
    static_cast<void>(domain_id);
    Info participant_info = StatisticsBackend::get_info(participant_id);

    if (status.current_count_change == 1)
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Participant with GUID " << std::string(participant_info[GUID_TAG]) << " discovered." << std::endl;
    }
    else
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Participant with GUID " << std::string(participant_info[GUID_TAG]) << " update info." <<
            std::endl;
    }
}

void Monitor::Listener::on_datareader_discovery(
        EntityId domain_id,
        EntityId datareader_id,
        const DomainListener::Status& status)
{
    static_cast<void>(domain_id);
    Info datareader_info = StatisticsBackend::get_info(datareader_id);

    if (!datareader_info[METATRAFFIC_TAG])
    {
        if (status.current_count_change == 1)
        {
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) DataReader with GUID " << std::string(datareader_info[GUID_TAG]) << " discovered." <<
                std::endl;
        }
        else
        {
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) DataReader with GUID " << std::string(datareader_info[GUID_TAG]) << " update info." <<
                std::endl;
        }
    }
}

void Monitor::Listener::on_datawriter_discovery(
        EntityId domain_id,
        EntityId datawriter_id,
        const DomainListener::Status& status)
{
    static_cast<void>(domain_id);
    Info datawriter_info = StatisticsBackend::get_info(datawriter_id);

    if (!datawriter_info[METATRAFFIC_TAG])
    {
        if (status.current_count_change == 1)
        {
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) DataWriter with GUID " << std::string(datawriter_info[GUID_TAG]) << " discovered." <<
                std::endl;
        }
        else
        {
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) DataWriter with GUID " << std::string(datawriter_info[GUID_TAG]) << " update info." <<
                std::endl;
        }
    }
}

void Monitor::Listener::on_host_discovery(
        EntityId host_id,
        const DomainListener::Status& status)
{
    Info host_info = StatisticsBackend::get_info(host_id);

    if (status.current_count_change == 1)
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Host " << std::string(host_info[NAME_TAG]) << " discovered." << std::endl;
    }
    else
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Host " << std::string(host_info[NAME_TAG]) << " update info." << std::endl;
    }
}

void Monitor::Listener::on_user_discovery(
        EntityId user_id,
        const DomainListener::Status& status)
{
    Info user_info = StatisticsBackend::get_info(user_id);

    if (status.current_count_change == 1)
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) User " << std::string(user_info[NAME_TAG]) << " discovered." << std::endl;
    }
    else
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) User " << std::string(user_info[NAME_TAG]) << " update info." << std::endl;
    }
}

void Monitor::Listener::on_process_discovery(
        EntityId process_id,
        const DomainListener::Status& status)
{
    Info process_info = StatisticsBackend::get_info(process_id);

    if (status.current_count_change == 1)
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Process " << std::string(process_info[NAME_TAG]) << " discovered." << std::endl;
    }
    else
    {
        std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Process " << std::string(process_info[NAME_TAG]) << " update info." << std::endl;
    }
}

void Monitor::Listener::on_topic_discovery(
        EntityId domain_id,
        EntityId topic_id,
        const DomainListener::Status& status)
{
    static_cast<void>(domain_id);
    Info topic_info = StatisticsBackend::get_info(topic_id);

    if (!topic_info[METATRAFFIC_TAG])
    {
        if (status.current_count_change == 1)
        {
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Topic " << std::string(topic_info[NAME_TAG]) << " discovered." << std::endl;
        }
        else
        {
            std::cout << "(" << duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() << " ms) Topic " << std::string(topic_info[NAME_TAG]) << " update info." << std::endl;
        }
    }
}

/***************************************************************
* Utils
***************************************************************/
std::string Monitor::timestamp_to_string(
        const Timestamp timestamp)
{
    auto timestamp_t = std::chrono::system_clock::to_time_t(timestamp);
    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count();
    msec %= 1000;
    std::stringstream ss;

#ifdef _WIN32
    struct tm timestamp_tm;
    _localtime64_s(&timestamp_tm, &timestamp_t);
    ss << std::put_time(&timestamp_tm, "%F %T") << "." << std::setw(3) << std::setfill('0') << msec;
#else
    ss << std::put_time(localtime(&timestamp_t), "%F %T") << "." << std::setw(3) << std::setfill('0') << msec;
#endif // ifdef _WIN32

    return ss.str();
}




static void print_usage(const char* prog)
{
    std::cout <<
R"(Usage:
  )" << prog << R"( [options]

Options:
  -d, --domain   <uint>   DDS domain id (default: 0)
  -b, --bins     <uint>   Number of histogram bins (default: 1)
  -i, --interval <uint>   Query window / period (seconds) (default: 5)
  -t, --topic    <name>   Target topic to monitor (default: SecureMulticastTopic)
  -o, --output   <path>   Dump DB to '<path>_<timestamp>.json' each cycle (default: disabled)
  -r, --reset             Clear inactive entities after each dump (default: OFF)
                          (use --no-reset to keep them)
  -h, --help              Show this help and exit

Examples:
  )" << prog << R"( --domain 0 --bins 1 --interval 10
  )" << prog << R"( -o stats/db_dump --no-reset
)";
}

static bool parse_uint(const std::string& s, uint64_t max, uint64_t& out)
{
    if (s.empty() || !std::all_of(s.begin(), s.end(), ::isdigit)) return false;
    try {
        unsigned long long v = std::stoull(s);
        if (v > max) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}

static std::optional<Options> parse_args(int argc, char** argv)
{
    Options cfg;
    auto need = [&](int i, const char* name)->std::optional<std::string>{
        if (i + 1 >= argc) { std::cerr << "Missing value for " << name << "\n"; return std::nullopt; }
        return std::string(argv[i+1]);
    };

    for (int i=1; i<argc; ++i)
    {
        std::string a = argv[i];
        auto pos = a.find('=');
        std::string flag = (pos==std::string::npos)? a : a.substr(0,pos);
        std::string val  = (pos==std::string::npos)? "" : a.substr(pos+1);

        if (flag=="-h" || flag=="--help") { print_usage(argv[0]); return std::nullopt; }
        else if (flag=="-d" || flag=="--domain") {
            if (val.empty()) { auto v=need(i,"--domain"); if(!v) return std::nullopt; val=*v; ++i; }
            uint64_t tmp; if(!parse_uint(val, std::numeric_limits<uint32_t>::max(), tmp)) { std::cerr<<"Invalid --domain\n"; return std::nullopt; }
            cfg.domain = static_cast<uint32_t>(tmp);
        }
        else if (flag=="-b" || flag=="--bins") {
            if (val.empty()) { auto v=need(i,"--bins"); if(!v) return std::nullopt; val=*v; ++i; }
            uint64_t tmp; if(!parse_uint(val, std::numeric_limits<uint16_t>::max(), tmp) || tmp==0) { std::cerr<<"Invalid --bins (>=1)\n"; return std::nullopt; }
            cfg.bins = static_cast<uint16_t>(tmp);
        }
        else if (flag=="-i" || flag=="--interval") {
            if (val.empty()) { auto v=need(i,"--interval"); if(!v) return std::nullopt; val=*v; ++i; }
            uint64_t tmp; if(!parse_uint(val, std::numeric_limits<uint32_t>::max(), tmp) || tmp==0) { std::cerr<<"Invalid --interval (>=1)\n"; return std::nullopt; }
            cfg.interval_s = static_cast<uint32_t>(tmp);
        }
        else if (flag=="-t" || flag=="--topic") {               
            if (val.empty()) { auto v=need(i,"--topic"); if(!v) return std::nullopt; val=*v; ++i; }
            if (val.empty()) { std::cerr<<"Invalid --topic\n"; return std::nullopt; }
            cfg.topic = val;
        }
        else if (flag=="-o" || flag=="--output") {
            if (val.empty()) { auto v=need(i,"--output"); if(!v) return std::nullopt; val=*v; ++i; }
            cfg.dump_prefix = val;
        }
        else if (flag=="-r" || flag=="--reset") { cfg.reset = true; }
        else if (flag=="--no-reset") { cfg.reset = false; }
        else { std::cerr<<"Unknown option: "<<a<<"\n"; print_usage(argv[0]); return std::nullopt; }
    }
    return cfg;
}

int main(int argc, char** argv)
{
    auto parsed = parse_args(argc, argv);
    if (!parsed)
        return (argc > 1 ? 0 : 0); // help shown => 0; on parse error above we returned nullopt earlier as well

    const Options cfg = *parsed;

    Monitor monitor;
    // NOTE: removed the stray semicolon after the if-condition from your original code
    if (monitor.init(cfg.domain, cfg.bins, cfg.interval_s, cfg.topic, cfg.dump_prefix, cfg.reset))
    {
        monitor.run();
    }
    else
    {
        std::cerr << "Monitor initialization failed.\n";
        return 1;
    }
    return 0;
}
