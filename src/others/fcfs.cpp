#include <cstdint>
#include <list>
#include <set>
#include <unordered_map>
#include <batprotocol.hpp>
#include <intervalset.hpp>

#include "batsim_edc.h"

using namespace batprotocol;

struct SchedJob {
    std::string job_id;
    uint8_t nb_hosts;
};

// Global variables for scheduler state
MessageBuilder * mb = nullptr;
bool format_binary = true;
std::list<SchedJob*> * jobs = nullptr;
std::unordered_map<std::string, SchedJob*> running_jobs;
std::unordered_map<std::string, std::set<uint32_t>> job_allocations;
uint32_t platform_nb_hosts = 0;
std::set<uint32_t> available_res;  

// -------------------------
// Initialization function
// -------------------------
uint8_t batsim_edc_init(const uint8_t * data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("Unknown flags used, cannot initialize myself.\n");
        return 1;
    }

    mb = new MessageBuilder(!format_binary);
    jobs = new std::list<SchedJob*>();

    return 0;
}

// -------------------------
// Deinitialization function
// -------------------------
uint8_t batsim_edc_deinit() {
    delete mb;
    mb = nullptr;

    if (jobs != nullptr) {
        for (auto * job : *jobs) {
            delete job;
        }
        delete jobs;
        jobs = nullptr;
    }

    running_jobs.clear();
    job_allocations.clear();
    return 0;
}

// -------------------------
// Decision (scheduling) function
// -------------------------
uint8_t batsim_edc_take_decisions(
    const uint8_t * what_happened,
    uint32_t what_happened_size,
    uint8_t ** decisions,
    uint32_t * decisions_size)
{
    (void) what_happened_size;
    auto * parsed = deserialize_message(*mb, !format_binary, what_happened);
    mb->clear(parsed->now());

    auto nb_events = parsed->events()->size();
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto event = (*parsed->events())[i];
        printf("fcfs received event type='%s'\n", batprotocol::fb::EnumNamesEvent()[event->event_type()]);

        switch (event->event_type()) {
            case fb::Event_BatsimHelloEvent: {
                mb->add_edc_hello("fcfs", "1.0.0");
            } break;

            case fb::Event_SimulationBeginsEvent: {
                auto simu_begins = event->event_as_SimulationBeginsEvent();
                platform_nb_hosts = simu_begins->computation_host_number();
                
                // Initialize available resources (hosts are numbered from 0 to platform_nb_hosts-1)
                for (uint32_t i = 0; i < platform_nb_hosts; i++) {
                    available_res.insert(i);
                }
            } break;

            case fb::Event_JobSubmittedEvent: {
                auto parsed_job = event->event_as_JobSubmittedEvent();
                auto job = new SchedJob();
                job->job_id = parsed_job->job_id()->str();
                job->nb_hosts = parsed_job->job()->resource_request();
                
                // Reject jobs that request more hosts than available on the platform
                if (job->nb_hosts > platform_nb_hosts) {
                    mb->add_reject_job(job->job_id);
                    delete job;
                } else {
                    jobs->push_back(job);
                }
            } break;

            case fb::Event_JobCompletedEvent: {
                auto parsed_job = event->event_as_JobCompletedEvent();
                std::string completed_job_id = parsed_job->job_id()->str();

                if (running_jobs.count(completed_job_id)) { // Vérifie si le job existe toujours
                    SchedJob* completed_job = running_jobs[completed_job_id]; 
                    
                    // Libération des ressources
                    for (uint32_t host : job_allocations[completed_job_id]) {
                        available_res.insert(host);
                    }

                    running_jobs.erase(completed_job_id);
                    job_allocations.erase(completed_job_id);
                    delete completed_job; // supr manuel du job
                }
            } break;


            default: break;
        }
    }

    //  si des ressources sont disponibles on paut exécuter les jobs qui arrivent
    while (!jobs->empty()) {
        SchedJob* job = jobs->front();
        std::set<uint32_t> job_resources;

        // Vérifier si inaf de ressources sont disponibles
        if (available_res.size() >= job->nb_hosts) { 
            auto it = available_res.begin();
            for (uint8_t i = 0; i < job->nb_hosts; ++i, ++it) {
                job_resources.insert(*it);
            }

            // Suppr les ressources allouées
            for (uint32_t res : job_resources) {
                available_res.erase(res); // retirer de availab les ressources qu'on consacre au job en cours
            }

            running_jobs[job->job_id] = job;
            job_allocations[job->job_id] = job_resources;

            //auto hosts = IntervalSet(IntervalSet::ClosedInterval(0, job->nb_hosts-1));
            
            std::string resources_str;
            for (auto it = job_resources.begin(); it != job_resources.end(); ++it) {
                if (it != job_resources.begin()) resources_str += ",";
                resources_str += std::to_string(*it);
            }
            
            mb->add_execute_job(job->job_id, resources_str);

            jobs->pop_front();
        } else {
            
            break;
        }
    }

    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);
    return 0;
}