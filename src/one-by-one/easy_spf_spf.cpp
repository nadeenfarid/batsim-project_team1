#include <cstdint>
#include <list>
#include <set>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <batprotocol.hpp>
#include <intervalset.hpp>
#include "batsim_edc.h"

using namespace batprotocol;

// === Data structures ===
struct SchedJob {
    std::string job_id;
    uint32_t    nb_hosts;
    double      walltime;     // user‐provided bound p̃j
};

static MessageBuilder *mb = nullptr;
static bool format_binary = true;

static std::list<SchedJob*> *pending = nullptr;
static std::unordered_map<std::string, std::set<uint32_t>> allocations;
static std::unordered_map<std::string, double> end_times;
static std::set<uint32_t> available_hosts;
static uint32_t platform_nb_hosts = 0;

// === Helpers ===
static double compute_reservation(double now, uint32_t need_hosts) {
    uint32_t free_now = available_hosts.size();
    if (free_now >= need_hosts) return now;

    std::vector<std::pair<double, uint32_t>> events;
    for (const auto& kv : end_times) {
        double t_end = kv.second;
        uint32_t q = allocations[kv.first].size();
        events.emplace_back(t_end, q);
    }
    std::sort(events.begin(), events.end());
    for (const auto& ev : events) {
        free_now += ev.second;
        if (free_now >= need_hosts)
            return ev.first;
    }
    return events.empty() ? now : events.back().first;
}

static std::string allocate_hosts(const std::string &job_id, uint32_t q) {
    auto it = available_hosts.begin();
    std::set<uint32_t> picked;
    for (uint32_t i = 0; i < q; ++i, ++it)
        picked.insert(*it);
    for (uint32_t h : picked)
        available_hosts.erase(h);
    allocations[job_id] = picked;

    std::string s;
    for (auto h_it = picked.begin(); h_it != picked.end(); ++h_it) {
        if (h_it != picked.begin()) s += ",";
        s += std::to_string(*h_it);
    }
    return s;
}

// === Batsim EDC interface ===
extern "C" uint8_t batsim_edc_init(const uint8_t*, uint32_t, uint32_t flags) {
    format_binary = (flags & BATSIM_EDC_FORMAT_BINARY);
    mb = new MessageBuilder(!format_binary);
    pending = new std::list<SchedJob*>();
    return 0;
}

extern "C" uint8_t batsim_edc_deinit() {
    delete mb;
    for (auto *j : *pending) delete j;
    delete pending;
    allocations.clear();
    end_times.clear();
    available_hosts.clear();
    return 0;
}

extern "C" uint8_t batsim_edc_take_decisions(
    const uint8_t *what_happened,
    uint32_t what_happened_size,
    uint8_t **decisions,
    uint32_t *decisions_size)
{
    (void)what_happened_size;
    auto *msg = deserialize_message(*mb, !format_binary, what_happened);
    double now = msg->now();
    mb->clear(now);

    // 1. Handle events
    for (auto *ev : *msg->events()) {
        switch (ev->event_type()) {
            case fb::Event_BatsimHelloEvent:
                mb->add_edc_hello("easy-spf", "1.0.0");
                break;

            case fb::Event_SimulationBeginsEvent: {
                auto b = ev->event_as_SimulationBeginsEvent();
                platform_nb_hosts = b->computation_host_number();
                for (uint32_t i = 0; i < platform_nb_hosts; ++i)
                    available_hosts.insert(i);
                break;
            }

            case fb::Event_JobSubmittedEvent: {
                auto s = ev->event_as_JobSubmittedEvent();
                auto *j = new SchedJob();
                j->job_id   = s->job_id()->str();
                j->nb_hosts = s->job()->resource_request();
                j->walltime = s->job()->walltime();
                if (j->nb_hosts > platform_nb_hosts) {
                    mb->add_reject_job(j->job_id);
                    delete j;
                } else {
                    pending->push_back(j);
                }
                break;
            }

            case fb::Event_JobCompletedEvent: {
                auto c = ev->event_as_JobCompletedEvent();
                auto jid = c->job_id()->str();
                if (allocations.count(jid)) {
                    for (auto h : allocations[jid])
                        available_hosts.insert(h);
                    allocations.erase(jid);
                    end_times.erase(jid);
                }
                break;
            }

            default: break;
        }
    }

    // 2. EASY-SPF-SPF loop
    bool progress = true;
    while (progress && !pending->empty()) {
        progress = false;

        // Copy and sort queue by SPF (smallest walltime first)
        std::vector<SchedJob*> sorted(pending->begin(), pending->end());
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](SchedJob* a, SchedJob* b) {
                             return a->walltime < b->walltime;
                         });

        SchedJob *head = sorted.front();
        if (available_hosts.size() >= head->nb_hosts) {
            auto res = allocate_hosts(head->job_id, head->nb_hosts);
            mb->add_execute_job(head->job_id, res);
            end_times[head->job_id] = now + head->walltime;
            pending->remove(head);
            progress = true;
            continue;
        }

        double reserve_t = compute_reservation(now, head->nb_hosts);

        for (auto *cand : sorted) {
            if (cand == head) continue;
            if (available_hosts.size() >= cand->nb_hosts
                && now + cand->walltime <= reserve_t) {
                auto res = allocate_hosts(cand->job_id, cand->nb_hosts);
                mb->add_execute_job(cand->job_id, res);
                end_times[cand->job_id] = now + cand->walltime;
                pending->remove(cand);
                progress = true;
            }
        }
    }

    // 3. Send execute/reject decisions
    mb->finish_message(now);
    serialize_message(*mb, !format_binary,
                      const_cast<const uint8_t **>(decisions),
                      decisions_size);
    return 0;
}
