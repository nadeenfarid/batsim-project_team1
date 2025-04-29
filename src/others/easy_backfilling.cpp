// backfilling.cpp
//
// A simple backfilling scheduler implementation for Batsim.
// This implementation uses a list (jobs) for the pending jobs queue,
// a set for available resources, and maps for running jobs and their allocations.

#include <cstdint>
#include <list>
#include <set>
#include <unordered_map>
#include <string>
#include <cstdio>
#include <iterator>
#include <sstream>
#include <batprotocol.hpp>
#include <intervalset.hpp>
#include "batsim_edc.h"

using namespace batprotocol;

struct SchedJob {
    std::string job_id;
    uint8_t nb_hosts;
};

// Global variables for scheduler state
static MessageBuilder *mb = nullptr;
static bool format_binary = true;
static std::list<SchedJob*> *jobs = nullptr;
static std::unordered_map<std::string, SchedJob*> running_jobs;
static std::unordered_map<std::string, std::set<uint32_t>> job_allocations;
static uint32_t platform_nb_hosts = 0;
static std::set<uint32_t> available_res;  

// -------------------------
// Initialization function
// -------------------------
extern "C" uint8_t batsim_edc_init(const uint8_t *data, uint32_t size, uint32_t flags) {
    (void)data; // Unused
    (void)size; // Unused

    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("Unknown flags used, cannot initialize backfilling scheduler.\n");
        return 1;
    }
    
    mb = new MessageBuilder(!format_binary);
    jobs = new std::list<SchedJob*>();
    
    return 0;
}

// -------------------------
// Deinitialization function
// -------------------------
extern "C" uint8_t batsim_edc_deinit() {
    delete mb;
    mb = nullptr;
    
    if (jobs != nullptr) {
        for (auto *job : *jobs) {
            delete job;
        }
        delete jobs;
        jobs = nullptr;
    }
    
    // Also clean up any running jobs (if still present)
    for (auto &pair : running_jobs) {
        delete pair.second;
    }
    running_jobs.clear();
    job_allocations.clear();
    available_res.clear();
    
    return 0;
}

// -------------------------
// Decision (scheduling) function
// -------------------------
extern "C" uint8_t batsim_edc_take_decisions(
    const uint8_t *what_happened,
    uint32_t what_happened_size,
    uint8_t **decisions,
    uint32_t *decisions_size)
{
    (void) what_happened_size;
    auto *parsed = deserialize_message(*mb, !format_binary, what_happened);
    mb->clear(parsed->now());
    
    auto nb_events = parsed->events()->size();
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto event = (*parsed->events())[i];
        printf("backfilling received event type='%s'\n", fb::EnumNamesEvent()[event->event_type()]);
        
        switch (event->event_type()) {
            case fb::Event_BatsimHelloEvent: {
                mb->add_edc_hello("backfilling", "1.0.0");
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
                
                // If the job is still running, free its resources
                if (running_jobs.count(completed_job_id)) {
                    SchedJob* completed_job = running_jobs[completed_job_id];
                    for (uint32_t host : job_allocations[completed_job_id]) {
                        available_res.insert(host);
                    }
                    running_jobs.erase(completed_job_id);
                    job_allocations.erase(completed_job_id);
                    delete completed_job;
                }
            } break;
            
            default:
                break;
        }
    }
    
    // -------------------------
    // Scheduling loop with backfilling
    // -------------------------
    while (!jobs->empty()) {
        // Always try to schedule the job at the front of the queue first.
        SchedJob* job = jobs->front();
        if (available_res.size() >= job->nb_hosts) {
            // The front job fits: allocate the first nb_hosts resources available.
            std::set<uint32_t> job_resources;
            auto it = available_res.begin();
            for (uint8_t i = 0; i < job->nb_hosts; ++i, ++it) {
                job_resources.insert(*it);
            }
            // Remove allocated resources from the available set.
            for (uint32_t res : job_resources) {
                available_res.erase(res);
            }
            running_jobs[job->job_id] = job;
            job_allocations[job->job_id] = job_resources;
            
            // Build a comma-separated list of allocated resource IDs.
            std::string resources_str;
            for (auto it = job_resources.begin(); it != job_resources.end(); ++it) {
                if (it != job_resources.begin())
                    resources_str += ",";
                resources_str += std::to_string(*it);
            }
            mb->add_execute_job(job->job_id, resources_str);
            jobs->pop_front();
        } else {
            // The front job does not fit: attempt to backfill one job from the rest of the queue.
            bool backfilled = false;
            // Start from the second job (if any).
            for (auto it = std::next(jobs->begin()); it != jobs->end(); ++it) {
                SchedJob* backfill_job = *it;
                if (available_res.size() >= backfill_job->nb_hosts) {
                    std::set<uint32_t> job_resources;
                    auto res_it = available_res.begin();
                    for (uint8_t i = 0; i < backfill_job->nb_hosts; ++i, ++res_it) {
                        job_resources.insert(*res_it);
                    }
                    for (uint32_t res : job_resources) {
                        available_res.erase(res);
                    }
                    running_jobs[backfill_job->job_id] = backfill_job;
                    job_allocations[backfill_job->job_id] = job_resources;
                    
                    std::string resources_str;
                    for (auto res_iter = job_resources.begin(); res_iter != job_resources.end(); ++res_iter) {
                        if (res_iter != job_resources.begin())
                            resources_str += ",";
                        resources_str += std::to_string(*res_iter);
                    }
                    mb->add_execute_job(backfill_job->job_id, resources_str);
                    
                    // Remove the backfilled job from the pending queue.
                    jobs->erase(it);
                    backfilled = true;
                    break; // Schedule at most one backfilled job in this decision cycle.
                }
            }
            // If no pending job (other than the front) can be scheduled, then break out.
            if (!backfilled) {
                break;
            } else {
                // (Optionally, you could continue the loop to check for more opportunities.)
                break;
            }
        }
    }
    
    mb->finish_message(parsed->now());
    serialize_message(*mb, !format_binary, const_cast<const uint8_t **>(decisions), decisions_size);
    return 0;
}

// -------------------------