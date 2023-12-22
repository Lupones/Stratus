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

#include <cstdint>
#include <memory>
#include <vector>

#include "app-task.hpp"
#include "policy.hpp"
#include "vm-task.hpp"

#ifndef DISK_UTILS_H
#define DISK_UTILS_H
#include "disk-utils.hpp"
#endif

struct Cos {
    uint32_t num;               // CLOS number
    uint64_t mask;              // Ways assigned mask
    int mbps;                   // Memory BW in MBps
    std::vector<uint32_t> cpus; // Associated CPUs

    Cos(uint32_t _num, uint64_t _mask, int _mbps,
        const std::vector<uint32_t> &_cpus = {})
        : num(_num), mask(_mask), mbps(_mbps), cpus(_cpus)
    {
    }
};

// Commandline options that can be setted using the config file
struct CmdOptions {
    double ti = 1; // Time interval
    uint32_t mi =
        std::numeric_limits<uint32_t>::max(); // Max number of intervals
    std::vector<std::string> event = {"ref-cycles",
                                      "instructions"}; // Events to monitor
    std::vector<uint32_t> cpu_affinity = {}; // CPUs to pin the manager to
    std::string perf = "PID";
};

void config_read(const std::string &path, const std::string &overlay,
                 CmdOptions &cmd_options, tasklist_t &tasklist,
                 std::vector<Cos> &coslist,
                 std::shared_ptr<cat::policy::Base> &catpol);
