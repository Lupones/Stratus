/*
 * Copyright 2023 Universitat Politècnica de València

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once
#include <map>
#include <vector>

#include "intel-rdt.hpp"

#include "log.hpp"
#include "throw-with-trace.hpp"
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#ifndef DISK_UTILS_H
#define DISK_UTILS_H
#include "disk-utils.hpp"
#endif

namespace mi = boost::multi_index;

struct evlist;

struct Counter {
    int id = 0;
    std::string name = "";
    double value = 0;
    std::string unit = "";
    bool snapshot = false;
    uint64_t enabled = 0;
    uint64_t running = 0;

    Counter() = default;
    Counter(int _id, const std::string &_name, double _value,
            const std::string &_unit, bool _snapshot, uint64_t _enabled,
            uint64_t _running)
        : id(_id), name(_name), value(_value), unit(_unit), snapshot(_snapshot),
          enabled(_enabled), running(_running){};
    bool operator<(const Counter &c) const
    {
        return id < c.id;
    }
};

struct by_id {
};
struct by_name {
};
typedef mi::multi_index_container<
    Counter,
    mi::indexed_by<
        // sort by Counter::operator<
        mi::ordered_unique<mi::tag<by_id>, mi::identity<Counter>>,

        // sort by less<string> on name
        mi::ordered_unique<mi::tag<by_name>,
                           mi::member<Counter, std::string, &Counter::name>>>>
    counters_t;

class Perf
{
    const int max_num_events = 32;

    struct EventDesc {
        std::vector<struct evlist *> groups;

        EventDesc() = default;
        EventDesc(const std::vector<struct evlist *> &_groups)
            : groups(_groups){};
        void append(struct evlist *ev_list)
        {
            groups.push_back(ev_list);
        }
    };

    std::map<int32_t, EventDesc> id_events;
    bool initialized = false;
    std::string perf_type;

  public:
    Perf() = default;

    // Allow move members
    Perf(Perf &&) = default;
    Perf &operator=(Perf &&) = default;

    // Delete copy members
    Perf(const Perf &) = delete;
    Perf &operator=(const Perf &) = delete;

    ~Perf() = default;

    void set_perf_type(const std::string type);
    std::string get_perf_type();
    void init();
    void clean();
    void clean(int32_t id);
    void setup_events(int32_t id, const std::vector<std::string> &groups);
    std::vector<counters_t>
    read_counters(pid_t pid, int32_t id, double llc_occup_value,
                  double lmem_bw_value, double tmem_bw_value,
                  double rmem_bw_value, DiskUtils DU, float network_bwtx,
                  float network_bwrx, double ovs_bwtx, double ovs_bwrx,
                  uint64_t time_interval);
    std::vector<counters_t>
    read_counters(pid_t pid, int32_t id, double llc_occup_value,
                  double lmem_bw_value, double tmem_bw_value,
                  double rmem_bw_value, uint64_t time_interval);
    std::vector<std::vector<std::string>> get_names(int32_t id,
                                                    std::string type);
    void enable_counters(int32_t id);
    void disable_counters(int32_t id);
    void print_counters(int32_t id);
};

uint64_t read_max_ujoules_ram();
uint64_t read_max_ujoules_pkg();

