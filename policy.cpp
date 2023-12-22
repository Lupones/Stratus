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

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <memory>
#include <tuple>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <fmt/format.h>

#include "policy.hpp"
#include "log.hpp"
#include "stats.hpp"
//#include "disk-utils.hpp"
//#include "intel-cmt-cat/lib/pqos.h"

#ifndef WIN_SIZE
#define WIN_SIZE 30
#endif

namespace cat
{
namespace policy
{
namespace acc = boost::accumulators;

#define ACC boost::accumulators
typedef ACC::accumulator_set<
    double, ACC::stats<ACC::tag::last, ACC::tag::sum, ACC::tag::mean,
                       ACC::tag::variance, ACC::tag::rolling_mean,
                       ACC::tag::rolling_variance, ACC::tag::rolling_count>>
    accum_t;
#undef ACC

using std::string;
using fmt::literals::operator""_format;

const char *vm_categories_map[] = {"Invalid", "CPU/Mem", "CPU/Mem Low",
                                   "Disk",    "Disk RD", "Disk WR",
                                   "Network", "Unknown"};

// Test partitioning policy
void Test::apply(uint64_t current_interval, double interval_time,
                 double adjust_interval_time, const tasklist_t &tasklist)
{
    // Apply only when the amount of intervals specified has passed
    if (current_interval % every != 0)
        return;

    LOGINF("Policy name: Test");

    for (const auto &task_ptr : tasklist) {
        const Task &task = *task_ptr;
        uint32_t taskID = task.id;
        std::string taskName = task.name;
        for (uint32_t i = 0; i < task.cpus.size(); i++) {
            pid_t taskPID = task.pids[i];
            LOGINF("Task {}: ID {}, PID {}"_format(taskName, taskID, taskPID));
        }

        // READ INTEL EVENT COUNTERS
        /*double llc_occup = cat->monitor_get_values(taskPID,
        PQOS_MON_EVENT_L3_OCCUP); double lmem_bw = cat->monitor_get_values(taskPID,
        PQOS_MON_EVENT_LMEM_BW); double tmem_bw = cat->monitor_get_values(taskPID,
        PQOS_MON_EVENT_TMEM_BW); double rmem_bw = cat->monitor_get_values(taskPID,
        PQOS_MON_EVENT_RMEM_BW);*/

        // LIMIT NETWORK BANDWIDTH
        /*if (current_interval == 1)
        {
                net_setBwLimit(task, 5, 5, 5, 5, 5, 5);
                LOGINF("Net_BW limited for interface
        {}"_format(task.network_interface));
        }
        */

        // TEST INTEL CAT AND MBA
        if ((current_interval > 1) & (current_interval < 7)) {
            uint32_t clos;

            // TEST 1. ADD TASK BY PID TO CLOS AND RETRIEVE PID'S CLOS
            for (uint32_t i = 0; i < task.cpus.size(); i++) {
                cat->add_task(current_interval, task.pids[i]);
                clos = cat->get_clos_of_task(task.pids[i]);
                LOGINF(
                    "---> Task with PID {} with task added to CLOS {}"_format(
                        task.pids[i], clos));
            }

            // TEST 2. GET CBMs OF CLOS
            uint64_t mask = cat->get_cbm(clos, 0, "data");
            LOGINF("---> CLOS {} has data mask {:x}"_format(clos, mask));
            mask = cat->get_cbm(clos, 0, "code");
            LOGINF("---> CLOS {} has code mask {:x}"_format(clos, mask));

            // TEST 3. MODIFY CODE MASK
            cat->set_cbm(clos, 0, 0x3, 1, "code");
            // TEST 4. MODIFY DATA MASK
            cat->set_cbm(clos, 0, 0xf, 1, "data");

            // TEST 5. PRINT MASKS OF EACH CLOS
            mask = cat->get_cbm(clos, 0, "code");
            LOGINF("---> CLOS {} now has code mask {:x}"_format(clos, mask));
            mask = cat->get_cbm(clos, 0, "data");
            LOGINF("---> CLOS {} now has data mask {:x}"_format(clos, mask));

            // TEST 6. GET MBA
            // void set_mb(uint32_t cos_id, uint32_t socket, int ctrl, unsigned mb);
            // uint64_t get_mb(uint32_t cos_id, uint32_t socket);
            uint64_t mb = cat->get_mb(clos, 0);
            LOGINF("---> Current MB is {} Mbps"_format(mb));

            // TEST 7. SET MBA
            mb = 2000 * clos;
            cat->set_mb(clos, 0, 1, mb);
            mb = cat->get_mb(clos, 0);
            LOGINF("---> New MB is {} Mbps"_format(mb));
        }
    }
}




} // namespace policy
} // namespace cat
