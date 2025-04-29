/**************************************************************
 *  easy_unified.cpp  —  ONE plug-in for all EASY queue orders
 *                       + optional threshold rescue
 *
 *  Arg string (3rd token after -l):
 *      "spf"            → primary=SPF, backfill=SPF,  no threshold
 *      "lqf,lpf"        → primary=LQF, backfill=LPF, no threshold
 *      "spf@20"         → SPF/SPF   + threshold 20 h
 *      "lqf,lpf@20"     → LQF/LPF   + threshold 20 h
 *
 *  Compile (no external EDC header needed):
 *      g++ -std=c++17 -O2 -fPIC -shared easy_unified.cpp \
 *          $(pkg-config --cflags --libs batsim) \
 *          -o build/libeasy_variants.so
 *************************************************************/
 #include <algorithm>
 #include <cstdint>
 #include <list>
 #include <set>
 #include <string>
 #include <unordered_map>
 #include <vector>
 
 #include <batprotocol.hpp>
 #include <intervalset.hpp>
 
 using namespace batprotocol;
 
 /* ------------------------------------------------------------------------- */
 /*  Missing-header fallback (older Batsim)                                    */
 #ifndef BATSIM_EDC_FORMAT_BINARY
 # define BATSIM_EDC_FORMAT_BINARY 1u
 #endif
 /* ------------------------------------------------------------------------- */
 
 struct SchedJob {
     std::string job_id;
     uint32_t    nb_hosts;
     double      walltime;
     double      submit_time;
 };
 
 /* globals */
 static MessageBuilder *mb               = nullptr;
 static bool            format_bin       = true;
 static std::list<SchedJob*>            *pending = nullptr;
 static std::unordered_map<std::string, std::set<uint32_t>> allocations;
 static std::unordered_map<std::string, double>              end_times;
 static std::set<uint32_t>                                   available_hosts;
 static uint32_t platform_nb_hosts = 0;
 
 /* ------------------------------------------------------------------------- */
 /*  Policies                                                                 */
 enum class Policy { EXP, FCFS, LCFS, LPF, LQF, SPF, SQF };
 static Policy primary_policy  = Policy::FCFS;
 static Policy backfill_policy = Policy::FCFS;
 
 static const std::unordered_map<std::string, Policy> STR2POL = {
     {"exp",Policy::EXP},{"fcfs",Policy::FCFS},{"lcfs",Policy::LCFS},
     {"lpf",Policy::LPF},{"lqf",Policy::LQF},{"spf",Policy::SPF},
     {"sqf",Policy::SQF}
 };
 
 /* optional threshold (seconds); <0 ⇒ disabled */
 static double THRESHOLD_SEC = -1.0;
 
 /* ------------------------------------------------------------------------- */
 /* key function                                                              */
 static double key_for(const SchedJob* j, double now, Policy p)
 {
     switch (p) {
         case Policy::FCFS: return  j->submit_time;
         case Policy::LCFS: return -j->submit_time;
         case Policy::SQF : return  j->nb_hosts;
         case Policy::LQF : return -static_cast<double>(j->nb_hosts);
         case Policy::SPF : return  j->walltime;
         case Policy::LPF : return -j->walltime;
         case Policy::EXP : return -( (now - j->submit_time + j->walltime) /
                                      j->walltime );
     }
     return 0;
 }
 
 /* ------------------------------------------------------------------------- */
 /* helpers                                                                   */
 static double compute_reservation(double now, uint32_t need)
 {
     uint32_t free = available_hosts.size();
     if (free >= need) return now;
 
     std::vector<std::pair<double,uint32_t>> events;
     for (auto& kv : end_times)
         events.emplace_back(kv.second, allocations[kv.first].size());
     std::sort(events.begin(), events.end());
 
     for (auto& ev : events) {
         free += ev.second;
         if (free >= need) return ev.first;
     }
     return events.empty() ? now : events.back().first;
 }
 
 static std::string allocate(const std::string& jid, uint32_t q)
 {
     auto it = available_hosts.begin();
     std::set<uint32_t> picked;
     for (uint32_t i=0;i<q;++i,++it) picked.insert(*it);
     for (uint32_t h:picked) available_hosts.erase(h);
     allocations[jid] = picked;
 
     std::string s;
     for (auto h_it=picked.begin(); h_it!=picked.end(); ++h_it) {
         if (h_it!=picked.begin()) s += ",";
         s += std::to_string(*h_it);
     }
     return s;
 }
 
 /* ------------------------------------------------------------------------- */
 /*  EDC callbacks                                                            */
 extern "C" uint8_t
 batsim_edc_init(const uint8_t *arg, uint32_t arg_sz, uint32_t flags)
 {
     format_bin = (flags & BATSIM_EDC_FORMAT_BINARY);
     mb      = new MessageBuilder(!format_bin);
     pending = new std::list<SchedJob*>();
 
     /* parse argument */
     if (arg && arg_sz) {
         std::string s(reinterpret_cast<const char*>(arg), arg_sz);
         s.erase(remove_if(s.begin(), s.end(),
                           [](char c){return c=='\''||c=='\"';}), s.end());
 
         size_t at = s.find('@');
         std::string queue_part = (at==std::string::npos)? s : s.substr(0,at);
         if (at != std::string::npos)
             THRESHOLD_SEC = std::stod(s.substr(at+1)) * 3600.0; // h→s
 
         size_t comma = queue_part.find(',');
         std::string p1 = (comma==std::string::npos)? queue_part
                                                    : queue_part.substr(0,comma);
         std::string p2 = (comma==std::string::npos)? p1
                                                    : queue_part.substr(comma+1);
         if (auto it=STR2POL.find(p1); it!=STR2POL.end()) primary_policy=it->second;
         if (auto it=STR2POL.find(p2); it!=STR2POL.end()) backfill_policy=it->second;
     }
     return 0;
 }
 
 extern "C" uint8_t batsim_edc_deinit()
 {
     delete mb;
     for (auto*j:*pending) delete j;
     delete pending;
     allocations.clear(); end_times.clear(); available_hosts.clear();
     return 0;
 }
 
 extern "C" uint8_t
 batsim_edc_take_decisions(const uint8_t *what, uint32_t,
                           uint8_t **decisions, uint32_t *dsz)
 {
     auto *msg = deserialize_message(*mb, !format_bin, what);
     double now = msg->now();
     mb->clear(now);
 
     /* events */
     for (auto *ev : *msg->events()) {
         switch(ev->event_type()) {
             case fb::Event_BatsimHelloEvent:
                 mb->add_edc_hello("easy-unified", "1.2"); break;
 
             case fb::Event_SimulationBeginsEvent: {
                 auto b = ev->event_as_SimulationBeginsEvent();
                 platform_nb_hosts = b->computation_host_number();
                 for (uint32_t i=0;i<platform_nb_hosts;++i) available_hosts.insert(i);
                 break;
             }
             case fb::Event_JobSubmittedEvent: {
                 auto s = ev->event_as_JobSubmittedEvent();
                 auto *j = new SchedJob();
                 j->job_id      = s->job_id()->str();
                 j->nb_hosts    = s->job()->resource_request();
                 j->walltime    = s->job()->walltime();
                 j->submit_time = now;
                 if (j->nb_hosts>platform_nb_hosts)
                     mb->add_reject_job(j->job_id), delete j;
                 else pending->push_back(j);
                 break;
             }
             case fb::Event_JobCompletedEvent: {
                 auto c=ev->event_as_JobCompletedEvent();
                 auto jid=c->job_id()->str();
                 if (allocations.count(jid)) {
                     for(uint32_t h:allocations[jid]) available_hosts.insert(h);
                     allocations.erase(jid); end_times.erase(jid);
                 }
                 break;
             }
             default: break;
         }
     }
 
     /* EASY loop */
     bool progress=true;
     while(progress && !pending->empty()) {
         progress=false;
 
         /* unified sort: old jobs first, then policy */
         pending->sort([&](SchedJob* a,SchedJob* b){
             bool a_old = (THRESHOLD_SEC >= 0.0) &&
                          ((now - a->submit_time) > THRESHOLD_SEC);
             bool b_old = (THRESHOLD_SEC >= 0.0) &&
                          ((now - b->submit_time) > THRESHOLD_SEC);
             if (a_old != b_old)
                 return a_old;                       // old before new
             return key_for(a,now,primary_policy) < key_for(b,now,primary_policy);
         });
 
         SchedJob* head=pending->front();
 
         if (available_hosts.size()>=head->nb_hosts) {
             auto res=allocate(head->job_id, head->nb_hosts);
             mb->add_execute_job(head->job_id,res);
             end_times[head->job_id]=now+head->walltime;
             pending->pop_front(); progress=true; continue;
         }
 
         double reserve_t=compute_reservation(now, head->nb_hosts);
 
         std::vector<SchedJob*> bf(std::next(pending->begin()), pending->end());
         std::sort(bf.begin(), bf.end(), [&](SchedJob* a,SchedJob* b){
             return key_for(a,now,backfill_policy) < key_for(b,now,backfill_policy);
         });
 
         for(SchedJob* cand:bf){
             if(available_hosts.size()>=cand->nb_hosts &&
                now+cand->walltime<=reserve_t)
             {
                 auto res=allocate(cand->job_id,cand->nb_hosts);
                 mb->add_execute_job(cand->job_id,res);
                 end_times[cand->job_id]=now+cand->walltime;
                 pending->remove(cand); progress=true;
             }
         }
     }
 
     mb->finish_message(now);
     serialize_message(*mb, !format_bin,
                       const_cast<const uint8_t **>(decisions), dsz);
     return 0;
 }
 