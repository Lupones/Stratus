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
#include <array>
#include <clocale>
#include <csignal>
#include <iostream>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/stacktrace.hpp>
#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>

#include "app-task.hpp"
#include "policy.hpp"
#include "common.hpp"
#include "config.hpp"
#include "events-perf.hpp"
#include "intel-rdt.hpp"
#include "log.hpp"
#include "net-bandwidth.hpp"
#include "stats.hpp"
#include "vm-task.hpp"

const char *input_name;

namespace chr = std::chrono;
namespace fs = boost::filesystem;
namespace po = boost::program_options;

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::to_string;
using std::vector;
using std::this_thread::sleep_for;
using fmt::literals::operator""_format;

typedef std::shared_ptr<IntelRDT> CAT_ptr_t;
typedef std::chrono::system_clock::time_point time_point_t;

CAT_ptr_t cat_setup(const vector<Cos> &coslist);
void loop(tasklist_t &tasklist, std::shared_ptr<cat::policy::Base> catpol,
          Perf &perf, const vector<string> &events, uint64_t time_int_us,
          uint32_t max_int, std::ostream &out, std::ostream &ucompl_out,
          std::ostream &total_out);
void clean(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf);
[[noreturn]] void clean_and_die(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf,
                                bool monitor_only);
std::string program_options_to_string(const std::vector<po::option> &raw);
void adjust_time(const time_point_t &start_int, const time_point_t &start_glob,
                 const uint64_t interval, const uint64_t time_int_us,
                 int64_t &adj_delay_us, bool new_task_completion);
void herod_the_great();
void sigint_handler(int signum);
void sigabrt_handler(int signum);

// Signal
jmp_buf return_to_top_level;

/* TODO: Read from template */
//std::string osd_path = "/home/jopucla/util/osd_stats.json";

CAT_ptr_t cat_setup(const vector<Cos> &coslist)
{
    LOGINF("Using Intel RDT - PQOS Interface");
    std::shared_ptr<IntelRDT> cat;
    cat = std::make_shared<IntelRDT>();
    cat->init();

    // Configure CLOS specified in the configuration template
    for (unsigned i = 0; i < coslist.size(); i++) {
        const auto &cos = coslist[i];

        // Set intial cache ways CAT (CDP DISABLED)
        // set_l3_clos(const unsigned cos_id, const uint64_t mask, const unsigned sock_num, int cdp, const unsigned scope)
        cat->set_l3_clos(cos.num, cos.mask, 0, 0, CAT_UPDATE_SCOPE_BOTH);
        int mask = cat->get_cbm(cos.num, 0);
        LOGINF("CLOS {} has initial mask 0x{:x}"_format(cos.num, mask));

        // Set intial memory bw MBA (if required)
        // set_mba_clos(const unsigned cos_id, const uint64_t mb, const unsigned sock_num, int ctrl)
        if (cos.mbps != -1) {
            cat->set_mba_clos(cos.num, cos.mbps, 0, 1);
            cat->set_mba_clos(cos.num, cos.mbps, 1, 1);
        }

        for (const auto &cpu : cos.cpus)
            cat->add_cpu(cos.num, cpu);
    }

    // If no CLOS specified, print CLOS 0 configuration
    if (coslist.size() == 0) {
        int mask = cat->get_cbm(0, 0);
        LOGINF("CLOS 0 has initial mask 0x{:x}"_format(mask));
        uint64_t mb = cat->get_mb(0, 0);
        LOGINF("CLOS 0 memory BW limit is {} Mbps"_format(mb));
    }

    return cat;
}

// Wait until the VMs are shutdown
void simple_loop(tasklist_t &tasklist,
                 std::shared_ptr<cat::policy::Base> catpol, Perf &perf,
                 const vector<string> &events, uint64_t time_int_us,
                 uint32_t max_int, std::ostream &out, std::ostream &ucompl_out,
                 std::ostream &total_out, std::ostream &times_out,
                 bool monitor_only)
{
    LOGINF("Inside simple loop");
    if (time_int_us <= 0)
        throw_with_trace(std::runtime_error(
            "Interval time must be positive and greater than 0"));
    if (max_int <= 0)
        throw_with_trace(
            std::runtime_error("Max time must be positive and greater than 0"));

    //LOGINF("Prepare Perf...");
    // Prepare Perf to measure events and initialize stats
    for (const auto &task_ptr : tasklist) {
        int num_cpu = 0;
        for (auto it = task_ptr->cpus.begin(); it != task_ptr->cpus.end();
             ++it, ++num_cpu) {
            if (task_ptr->pids[num_cpu] > 0) {
                LOGINF("PID: {}, CPU num. {}"_format(task_ptr->pids[0],
                                                     *it));

                task_ptr->stats[num_cpu] = Stats();

                //LOGINF("Perf_type: {}"_format(perf.get_perf_type()));
                if (perf.get_perf_type() == "PID") {
                    if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr)
                        task_ptr->stats[num_cpu].init(
                            perf.get_names((int)task_ptr->pids[num_cpu],
                                           "VM")[0],
                            (double)time_int_us / 1000 / 1000);
                    else
                        task_ptr->stats[num_cpu].init(
                            perf.get_names((int)task_ptr->pids[num_cpu],
                                           "app")[0],
                            (double)time_int_us / 1000 / 1000);
                } else if (perf.get_perf_type() == "CPU") {
                    if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr)
                        task_ptr->stats[num_cpu].init(
                            perf.get_names(*it, "VM")[0],
                            (double)time_int_us / 1000 / 1000);
                    else
                        task_ptr->stats[num_cpu].init(
                            perf.get_names(*it, "app")[0],
                            (double)time_int_us / 1000 / 1000);
                }
            }
        }
    }

    // Print headers
    tasklist[0]->task_stats_print_headers(out);
    tasklist[0]->task_stats_print_headers(ucompl_out);
    tasklist[0]->task_stats_print_headers(total_out);
    tasklist[0]->task_stats_print_times_headers(times_out);

    //LOGINF("First reading of counters");
    // First reading of counters
    for (const auto &task_ptr : tasklist) {
        std::shared_ptr<VMTask> vm_ptr =
            std::dynamic_pointer_cast<VMTask>(task_ptr);

        int num_cpu = 0;
        for (auto it = task_ptr->cpus.begin(); it != task_ptr->cpus.end();
             ++it, ++num_cpu) {
            counters_t counters;

            if (task_ptr->pids[num_cpu] > 0) {
                //LOGINF("1. Enable counters");
                if (perf.get_perf_type() == "PID")
                    perf.enable_counters((int)task_ptr->pids[num_cpu]);
                else if (perf.get_perf_type() == "CPU")
                    perf.enable_counters(*it);

                //LOGINF("2. Read counters");
                if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                    //LOGINF("I AM A VM TASK!");
                    vm_ptr->diskUtils.read_disk_stats(vm_ptr->dom);

                    if (perf.get_perf_type() == "PID")
                        counters = perf.read_counters(
                            vm_ptr->pids[num_cpu], (int)vm_ptr->pids[num_cpu],
                            task_ptr->llc_occup, task_ptr->lmem_bw,
                            task_ptr->tmem_bw, task_ptr->rmem_bw,
                            vm_ptr->diskUtils, 0, 0, 0, 0, 0)[0];
                    else if (perf.get_perf_type() == "CPU")
                        counters = perf.read_counters(
                            vm_ptr->pids[num_cpu], *it, task_ptr->llc_occup,
                            task_ptr->lmem_bw, task_ptr->tmem_bw,
                            task_ptr->rmem_bw, vm_ptr->diskUtils, 0, 0, 0, 0,
                            0)[0];

                } else if (std::dynamic_pointer_cast<AppTask>(task_ptr) !=
                           nullptr) {
                    //LOGINF("I AM AN APP TASK!");

                    if (perf.get_perf_type() == "PID")
                        counters = perf.read_counters(
                            task_ptr->pids[num_cpu],
                            (int)task_ptr->pids[num_cpu], task_ptr->llc_occup,
                            task_ptr->lmem_bw, task_ptr->tmem_bw,
                            task_ptr->rmem_bw, 0)[0];
                    else if (perf.get_perf_type() == "CPU")
                        counters = perf.read_counters(
                            task_ptr->pids[num_cpu], *it, task_ptr->llc_occup,
                            task_ptr->lmem_bw, task_ptr->tmem_bw,
                            task_ptr->rmem_bw, 0)[0];
                }

                //LOGINF("3. Stats accumulate counters");
                task_ptr->stats[num_cpu].accum(counters, (double)time_int_us /
                                                             1000 / 1000);
            }
        }
    }

    // Reset Ceph counters
    //if (ceph_reset_stats() < 0)
    //	    LOGINF("WARNING: Can't reset Ceph stats in xpl5");

    // Limit network BW
    for (const auto &task_ptr : tasklist) {
        //if (task_ptr->name == "stress_ng_VM")
        //    continue;

        std::shared_ptr<VMTask> vm_ptr =
            std::dynamic_pointer_cast<VMTask>(task_ptr);
        if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
            VMTask &task = *vm_ptr;
            net_setBwLimit(task, task.netbw_in_avg, task.netbw_in_peak,
                           task.netbw_in_burst, task.netbw_out_avg,
                           task.netbw_out_peak, task.netbw_out_burst);
        }
    }

    /**** LOOP UNTIL END OF EXECUTION ****/
    uint32_t interval;
    int64_t adj_delay_us = time_int_us;
    auto start_glob = std::chrono::system_clock::now();
    auto t1 = std::chrono::system_clock::now(); //measure overhead algorithm
    auto t2 = std::chrono::system_clock::now();
    uint64_t total_elapsed_us = 0;
    bool new_task_completion = false;
    tasklist_t runlist = tasklist_t(tasklist); // Tasks that are not done
    //nlohmann::json ceph_stats_then, ceph_stats_now;

    for (interval = 0; interval < max_int; interval++) {
        std::vector<CPUData> entries1;
        std::vector<CPUData> entries2;
        struct timeval then, now;

        auto start_int = std::chrono::system_clock::now();
        bool all_completed =
            true; // Have all the tasks reached their execution limit?

        LOGINF("**** Starting interval {} - {} us ****"_format(
            interval,
            chr::duration_cast<chr::microseconds>(start_int - start_glob)
                .count()));

        // Calculate overhead
        t2 = std::chrono::system_clock::now();
        if (interval > 0) {
            uint64_t elapsed_us =
                std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1)
                    .count();
            uint32_t prev_interval = interval - 1;
            LOGINF("[OVERHEAD] Interval {} - {} = {} us"_format(
                interval, prev_interval, elapsed_us));
            total_elapsed_us = total_elapsed_us + elapsed_us;
        }

        //----> 1. Pre-sleep calculations
        for (const auto &task_ptr : runlist) {
            //if (task_ptr->name == "stress_ng_VM")
            //    continue;

            std::shared_ptr<VMTask> vm_ptr =
                std::dynamic_pointer_cast<VMTask>(task_ptr);

            if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                VMTask &task = *vm_ptr;
                // Get then stats for VM CPU utilization
                if ((task.then_nparams = virDomainGetCPUStats(
                         task.dom, task.then_params, task.nparams, 0,
                         task.max_id, 0)) < 0) {
                    LOGINF("WARNING: Can't get domain CPU stats (then) of " +
                           task.domain_name);
                }

                // Read network BW 1
                // NOTE: retreiving this BW has a very high overhead
                //net_getBwBytes(task, &task.read_network_bwrx,
                //               &task.read_network_bwtx);

                //Read OVS BW 1
                ovs_ofctl_poll_stats(task.domain_name, &task.ovs_bwrx,
                                     &task.ovs_bwtx);

                // Bring Ceph data at the beginning of the interval
                /*
                if (ceph_get_json_stats_file(osd_path) < 0) {
                        LOGINF("WARNING: Can't get Ceph stats (then)");
                }
                else
                {
                        ceph_stats_then = ceph_parse_json_stats_file(osd_path);
                        ceph_get_stat(ceph_stats_then, "op_latency", &task.ceph_op_latency);
                }
                */
            }

            // Read CPU USAGE 1
            ReadStatsCPU(entries1);
        }

        // Get current time (pre-sleep)
        if (gettimeofday(&then, NULL) < 0) {
            throw_with_trace(std::runtime_error("Unable to get time (then)"));
        }

        //----> 2. SLEEP
        sleep_for(chr::microseconds(adj_delay_us));

        // Get current time (post-sleep)
        if (gettimeofday(&now, NULL) < 0) {
            throw_with_trace(std::runtime_error("Unable to get time (now)"));
        }

        //LOGDEB("Slept for {} us"_format(adj_delay_us));
        LOGINF("Slept for {} us"_format(adj_delay_us));

        t1 = std::chrono::system_clock::now();

        // Save adj_delay_us in seconds
        double interval_ti = (double)(adj_delay_us) / 1000000;
        new_task_completion = false;

        // Control elapsed time
        adjust_time(start_int, start_glob, interval, time_int_us, adj_delay_us,
                    new_task_completion);

        //----> 3. Post-sleep calculations
        bool all_started = true;
        for (const auto &task_ptr : runlist) {
            //if (task_ptr->name == "stress_ng_VM")
            //    continue;

            uint64_t current_time;

            // Read current time
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            current_time =
                ((ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec) / 1000000) %
                1000000;

            // Read CPU USAGE 2
            ReadStatsCPU(entries2);

            // Class Pointers
            std::shared_ptr<VMTask> vm_ptr =
                std::dynamic_pointer_cast<VMTask>(task_ptr);
            std::shared_ptr<AppTask> app_ptr =
                std::dynamic_pointer_cast<AppTask>(task_ptr);

            if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                VMTask &task = *vm_ptr;

                // Check if clients have already written the STARTED file
                if ((!monitor_only) & (task.client) &
                    (task.name != "iperf_VM")) {
                    std::string filename = "/homenvm/dsf_" +
                                           std::string(task.domain_name) +
                                           "/STARTED";
                    all_started &= fs::exists(filename);
                    if ((fs::exists(filename)) & (!task.client_started)) {
                        task.client_started = true;
                        task.interval_start = interval;
                        LOGINF("Interval start for {}:{} is {}"_format(
                            task.id, task.name, task.interval_start));
                    }
                    LOGINF("Task {} client started: {}"_format(
                        task.name, fs::exists(filename)));
                } else {
                    all_started &= 1;
                    task.interval_start = 0;
                    if (interval == 0)
                        LOGINF("Interval start for {}:{} is {}"_format(
                            task.id, task.name, interval));
                }

                // Get now stats for VM CPU utilization
                if ((task.now_nparams = virDomainGetCPUStats(
                         task.dom, task.now_params, task.nparams, 0,
                         task.max_id, 0)) < 0) {
                    LOGINF("WARNING: Can't get domain CPU stats (now) of " +
                           task.domain_name);
                } /*else {
                    float vcpu_usage = task.task_get_VM_total_CPU_usage(
                        then.tv_sec * 1000000 + then.tv_usec,
                        now.tv_sec * 1000000 + now.tv_usec);
                    LOGINF("Total VM CPU(s) utilization of {}: {}%"_format(task.domain_name, vcpu_usage));
                }*/

                //long long rxdata = -1, txdata = -1;
                // Read network BW 2
                // NOTE: retreiving this BW has a very high overhead
                //time1 = std::chrono::system_clock::now();
                // net_getBwBytes(task, &rxdata, &txdata);
                // time2 = std::chrono::system_clock::now();
                // elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(time2 - time1).count();
                // LOGINF("[TIME net_getBwBytes] Elapsed time = {} us"_format(elapsed_time));
                //task.network_bwrx = (rxdata - task.read_network_bwrx) /
                //                    ((float)interval_ti) / 1024;
                //task.network_bwtx = (txdata - task.read_network_bwtx) /
                //                    ((float)interval_ti) / 1024;
                task.network_bwrx = 0;
                task.network_bwtx = 0;
                //LOGINF("Net_BW VM {}: \t Rx:{}KB/s\tTx:{}KB/s\tRxTx:{}KB/s"_format(task.domain_name, task.network_bwrx, task.network_bwtx,(task.network_bwrx + task.network_bwtx)));

                //Read OVS BW 1
                double rx = -1, tx = -1;
                ovs_ofctl_poll_stats(task.domain_name, &rx, &tx);
                task.ovs_bwrx =
                    (rx - task.ovs_bwrx) / ((double)interval_ti) / 1024;
                task.ovs_bwtx =
                    (tx - task.ovs_bwtx) / ((double)interval_ti) / 1024;
                //LOGINF("OVS_BW VM {}:\tRx:{}KB/s\tTx:{}KB/s\tRxTx:{}KB/s"_format(task.domain_name, task.ovs_bwrx, task.ovs_bwtx,(task.ovs_bwtx + task.ovs_bwrx)));

                if (!task.task_exited(monitor_only)) {
                    // Read disk utilization should precede perf_read_counters
                    // to add disk stats correctly to the csv
                    task.diskUtils.read_disk_stats(task.dom);
                    task.diskUtils.print_disk_stats_quantum(
                        task.dom, (double)time_int_us);
                }
            } else {
                all_started = 1;
            }

            int num_cpu = 0;
            uint64_t total_inst = 0;
            for (auto it = task_ptr->cpus.begin(); it != task_ptr->cpus.end();
                 ++it) {
                counters_t counters;

                // Get Intel RDT values
                if (perf.get_perf_type() == "PID")
                    catpol->get_cat()->monitor_get_values_pid(
                        task_ptr->pids[num_cpu], &task_ptr->llc_occup,
                        &task_ptr->lmem_bw, &task_ptr->tmem_bw,
                        &task_ptr->rmem_bw);
                else if (perf.get_perf_type() == "CPU")
                    catpol->get_cat()->monitor_get_values_core(
                        *it, &task_ptr->llc_occup, &task_ptr->lmem_bw,
                        &task_ptr->tmem_bw, &task_ptr->rmem_bw);

                // Get CPU utilzation of each core
                float util_core = get_cpu_utilization(entries1, entries2, *it);
                task_ptr->total_cpu_util[*it] = util_core;

                // Get TIME utilzation of each core
                std::array<std::string, 10> times = {
                    "user", "nice",    "system", "idle",  "iowait",
                    "irq",  "softirq", "steal",  "guest", "guest_nice"};
                for (const auto &time : times) {
                    util_core =
                        get_time_utilization(entries1, entries2, *it, time);
                    task_ptr->total_time_util[std::make_pair(time, *it)] =
                        util_core;
                }

                if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                    VMTask &task = *vm_ptr;

                    // Get VM CPU utilizations
                    float util_vm = task.task_get_VM_CPU_usage(
                        then.tv_sec * 1000000 + then.tv_usec,
                        now.tv_sec * 1000000 + now.tv_usec, *it);
                    task.vm_cpu_util[*it] = util_vm;

                    // Read counters
                    //LOGINF("Print stats for {}"_format(*it));
                    if (perf.get_perf_type() == "PID")
                        counters = perf.read_counters(
                            task.pids[num_cpu], (int)task.pids[num_cpu],
                            task.llc_occup, task.lmem_bw, task.tmem_bw,
                            task.rmem_bw, task.diskUtils, task.network_bwtx,
                            task.network_bwrx, task.ovs_bwtx, task.ovs_bwrx,
                            current_time)[0];
                    else if (perf.get_perf_type() == "CPU")
                        counters = perf.read_counters(
                            task.pids[num_cpu], *it, task.llc_occup,
                            task.lmem_bw, task.tmem_bw, task.rmem_bw,
                            task.diskUtils, task.network_bwtx,
                            task.network_bwrx, task.ovs_bwtx, task.ovs_bwrx,
                            current_time)[0];
                    task.stats[num_cpu].accum(counters, (double)time_int_us /
                                                            1000 / 1000);
                } else {
                    AppTask &task = *app_ptr;

                    // Read counters
                    if (task_ptr->pids[num_cpu] > 0) {
                        if (perf.get_perf_type() == "PID")
                            counters = perf.read_counters(
                                task.pids[num_cpu], (int)task.pids[num_cpu],
                                task.llc_occup, task.lmem_bw, task.tmem_bw,
                                task.rmem_bw, current_time)[0];
                        else if (perf.get_perf_type() == "CPU")
                            counters = perf.read_counters(
                                task.pids[num_cpu], *it, task.llc_occup,
                                task.lmem_bw, task.tmem_bw, task.rmem_bw,
                                current_time)[0];

                        task.stats[num_cpu].accum(
                            counters, (double)time_int_us / 1000 / 1000);
                        total_inst +=
                            task.stats[num_cpu].get_current("inst_retired.any");
                    }
                }
                num_cpu++;
            } // end for all cpus

            if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                VMTask &task = *vm_ptr;

                if (all_started) {
                    //if (task.paused)
                    //	task.task_resume();

                    task.task_stats_print_interval(interval, out, monitor_only);
                    task.task_stats_print_times_interval(interval, times_out,
                                                         monitor_only);
                }

                // Test if the application has completed (wrote APP_COMPLETED file on its data shared folder)
                // Previously, test if the VM has been shutdown (task completed)
                if (task.task_exited(monitor_only)) {
                    LOGINF("Task {} exited"_format(task.domain_name));
                    task.task_clear_exited();
                    task.set_status(Task::Status::exited);
                    task.completed++;
                    task.run_id++;
                    new_task_completion = true;
                    LOGINF("...done");
                } /*else {

                    if (all_started) {
                        task.task_stats_print_interval(interval, out, monitor_only);
                        task.task_stats_print_times_interval(interval, times_out, monitor_only);
                    }
                }*/
            } else {
                AppTask &task = *app_ptr;

                // Print interval stats
                task.task_stats_print_interval(interval, out, monitor_only);
                task.task_stats_print_times_interval(interval, times_out,
                                                     monitor_only);

                // Test if the instruction limit has been reached
                if (task.task_exited(monitor_only)) {
                    LOGINF("Task {} ({}) has finished!"_format(task.name,
                                                               task.pids[0]));
                    task.set_status(Task::Status::exited);
                    task.completed++;
                    task.run_id++;
                    new_task_completion = true;
                } else if (task.max_instr > 0 && total_inst >= task.max_instr) {
                    task.set_status(
                        Task::Status::
                            limit_reached); // Status can change from runnable -> limit_reached
                    task.completed++;
                    task.run_id++;
                }
            }

            // If any non-batch task is not completed then we don't finish
            if (!task_ptr->completed && !task_ptr->batch)
                all_completed = false;

            // It's the first time it finishes or reaches the instruction limit print acumulated stats until this point
            if (task_ptr->get_status() == Task::Status::limit_reached ||
                task_ptr->get_status() == Task::Status::exited) {
                if (task_ptr->completed == 1)
                    task_ptr->task_stats_print_total(interval, ucompl_out);
            }
        }

        // All the tasks have reached their limit -> finish execution
        if (all_completed) {
            LOGINF("[TOTAL OVERHEAD] {} us"_format(total_elapsed_us));
            LOGINF("--------------- ALL COMPLETED ---------------------");
            break;
        } else {
            //LOGINF("Not all completed... continue");
        }

        //----> 4. Post-processing actions
        for (const auto &task_ptr : runlist) {
            //if (task_ptr->name == "stress_ng_VM")
            //    continue;

            if (task_ptr->get_status() == Task::Status::exited) {
                LOGINF("Task {} has status EXITED"_format(task_ptr->name));

                // Deal with apps that finish or reach the limit
                task_ptr->task_restart_or_set_done(
                    catpol->get_cat(), perf,
                    events); // Status can change from (exited | limit_reached) -> done
                /*for (const auto &task_ptr_aux : runlist) {
                                if (task_ptr->id != task_ptr_aux->id) {
                                                LOGINF("Pause task {}:{}"_format(task_ptr_aux->id, task_ptr_aux->name));
                                                task_ptr_aux->task_pause();
                                }
                }*/

                // If it's done print total stats
                if (task_ptr->get_status() == Task::Status::done) {
                    task_ptr->task_stats_print_total(interval, total_out);
                    for (uint32_t i = 0; i < task_ptr->cpus.size(); i++) {
                        if (perf.get_perf_type() == "PID") {
                            catpol->get_cat()->monitor_stop_pid(
                                task_ptr->pids[i]);
                        } else if (perf.get_perf_type() == "CPU") {
                            catpol->get_cat()->monitor_stop_core(task_ptr->cpus[i]);
                        }
                    }
                }
            }
        }

        // Remove tasks that are done from runlist
        runlist.erase(std::remove_if(runlist.begin(), runlist.end(),
                                     [](const auto &task_ptr) {
                                         return ((task_ptr->get_status() ==
                                                  Task::Status::done) ||
                                                 (task_ptr->get_status() ==
                                                  Task::Status::exited));
                                     }),
                      runlist.end());
        assert(!runlist.empty());

        // Adjust CAT according to the selected policy
        //LOGINF("Applying CAT Policy in interval {} with interval_time {}"_format(interval, (double)(adj_delay_us) / 1000000));
        catpol->apply(interval, (double)time_int_us / 1000 / 1000, interval_ti,
                      runlist);

        //auto end_int = std::chrono::system_clock::now();
        //LOGINF("---> Interval {} duration - {} us"_format(interval,chr::duration_cast<chr::microseconds>(end_int - start_int).count()));
    }

    // Print acumulated stats for non completed tasks and total stats for all the tasks
    for (const auto &task_ptr : tasklist) {
        //if (task_ptr->name == "stress_ng_VM")
        //    continue;

        std::shared_ptr<VMTask> vm_ptr =
            std::dynamic_pointer_cast<VMTask>(task_ptr);

        if (!task_ptr->completed) {
            task_ptr->task_stats_print_total(interval, ucompl_out);
            task_ptr->task_stats_print_total(interval, total_out);
        }
        if (task_ptr->get_status() != Task::Status::done) {
            task_ptr->task_stats_print_total(interval, total_out);
            if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                VMTask &task = *vm_ptr;
                task.diskUtils.print_disk_stats(task.dom, time_int_us,
                                                interval);
            }
        }
    }
}

void adjust_time(const time_point_t &start_int, const time_point_t &start_glob,
                 const uint64_t interval, const uint64_t time_int_us,
                 int64_t &adj_delay_us, bool new_task_completion)
{
    // Control elapsed time
    auto now = std::chrono::system_clock::now();
    uint64_t elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_int)
            .count();
    uint64_t total_elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - start_glob)
            .count();

    uint64_t last = adj_delay_us;

    // Adjust time with a PI controller
    const double kp = 0.5;
    const double ki = 0.25;
    int64_t proportional = (int64_t)time_int_us - (int64_t)elapsed_us;
    int64_t integral =
        (int64_t)time_int_us * (interval + 1) - (int64_t)total_elapsed_us;

    adj_delay_us += kp * proportional;
    adj_delay_us += ki * integral;

    if (adj_delay_us < 0 && !new_task_completion) {
        last *= 0;
        LOGINF(
            "This interval ({}) was way too long. The next interval should last {} us. It will last {}."_format(
                interval, adj_delay_us, last));
        adj_delay_us = last;
    }
}

// Leave the machine in a consistent state
void clean(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf)
{
    LOGINF("Resetting CAT and performance counters...");
    cat->reset();
    cat->fini();
    perf.clean();

    // Try to drop privileges before killing anything
    LOGINF("Dropping privileges...");
    drop_privileges();

    LOGINF("Deleting run dirs if any...");
    try {
        for (const auto &task_ptr : tasklist) {
            if (std::dynamic_pointer_cast<AppTask>(task_ptr) != nullptr) {
                std::shared_ptr<AppTask> app_ptr =
                    std::dynamic_pointer_cast<AppTask>(task_ptr);
                fs::remove_all(app_ptr->rundir);
            }
        }
    } catch (const std::exception &e) {
        const auto st = boost::get_error_info<traced>(e);
        if (st)
            LOGERR(e.what() << std::endl << *st);
        else
            LOGERR(e.what());
    }

    LOGINF("Killing children...");
    herod_the_great();
}

void clean_and_die(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf,
                   bool monitor_only)
{
    LOGERR("--- PANIC, TRYING TO CLEAN ---");

    try {
        if (cat->is_initialized())
            cat->reset();
        cat->fini();
    } catch (const std::exception &e) {
        LOGERR("Could not reset and finish CAT: " << e.what());
    }

    try {
        perf.clean();
    } catch (const std::exception &e) {
        LOGERR("Could not clean the performance counters: " << e.what());
    }

    // If the task is client-server, try to shutdown the client VM
    if (!monitor_only) {
        for (const auto &task_ptr : tasklist) {
            std::shared_ptr<VMTask> vm_ptr =
                std::dynamic_pointer_cast<VMTask>(task_ptr);
            if ((task_ptr->client) &
                (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr)) {
                string ssh_command =
                    "ssh -p 3322 jofepre@xpl2.gap.upv.es "
                    "'LIBVIRT_DEFAULT_URI=qemu:///system virsh shutdown " +
                    vm_ptr->client_domain_name;
                system(ssh_command.c_str());
            }
        }
    }
    // Kill children
    herod_the_great();

    LOGFAT("Exit with error");
}

std::string program_options_to_string(const std::vector<po::option> &raw)
{
    string args;

    for (const po::option &option : raw) {
        // if(option.unregistered) continue; // Skipping unknown options

        if (option.value.empty())
            args += option.string_key.size() == 1
                        ? "-"
                        : "--" + option.string_key + "\n";
        else {
            // this loses order of positional options
            for (const std::string &value : option.value) {
                args += option.string_key.size() == 1 ? "-" : "--";
                args += option.string_key + ": ";
                args += value + "\n";
            }
        }
    }

    return args;
}

void open_output_streams(const string &int_str, const string &ucompl_str,
                         const string &total_str, const string &times_str,
                         std::shared_ptr<std::ostream> &int_out,
                         std::shared_ptr<std::ostream> &ucompl_out,
                         std::shared_ptr<std::ostream> &total_out,
                         std::shared_ptr<std::ostream> &times_out)
{
    // Open output file if needed; if not, use cout
    if (int_str == "") {
        int_out.reset(new std::ofstream());
        int_out->rdbuf(cout.rdbuf());
    } else
        int_out.reset(new std::ofstream(int_str));

    // Output file for summary stats until completion
    if (ucompl_str == "")
        ucompl_out.reset(new std::stringstream());
    else
        ucompl_out.reset(new std::ofstream(ucompl_str));

    // Output file for summary stats for all the time the applications have been executed, not only before they are completed
    if (total_str == "")
        total_out.reset(new std::stringstream());
    else
        total_out.reset(new std::ofstream(total_str));

    // Output file for summary stats for all the time the applications have been executed, not only before they are completed
    if (times_str == "")
        times_out.reset(new std::stringstream());
    else
        times_out.reset(new std::ofstream(times_str));
}

// He is good with spoiled children
void herod_the_great()
{
    auto children = std::vector<pid_t>();
    pid_get_children_rec(getpid(), children);
    if (children.empty())
        return;

    LOGDEB("Herod the Great has killed all the children he found: {}"_format(
        iterable_to_string(children.begin(), children.end(),
                           [](const auto &t) { return "{}"_format(t); },
                           ", ")));
    for (const auto &child_pid : children)
        if (kill(child_pid, SIGKILL) < 0)
            LOGERR("Could not SIGKILL pid '{}', is he Jesus?: {}"_format(
                child_pid, strerror(errno)));
}

void sigint_handler(int signum)
{
    LOGWAR("-- SIGINT received --");
    LOGWAR("Killing all child processes");
    herod_the_great();
    longjmp(return_to_top_level, 1);
    exit(signum);
}

void sigabrt_handler(int signum)
{
    LOGWAR("-- SIGABRT received --");
    LOGWAR("Killing all child processes");
    herod_the_great();
    longjmp(return_to_top_level, 1);
    exit(signum);
}

int main(int argc, char *argv[])
{
    srand(time(NULL));
    signal(SIGINT, sigint_handler);
    signal(SIGABRT, sigabrt_handler);

    // Set the locale to the one defined in the corresponding enviroment variable
    std::setlocale(LC_ALL, "");

    // Default log conf
    const string min_clog = "inf";
    const string min_flog = "inf";

    // Options that can be setted using the config file
    CmdOptions options;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "print usage message")(
        "config,c", po::value<string>()->required(),
        "pathname for yaml config file")(
        "config-override", po::value<string>()->default_value(""),
        "yaml string for overriding parts of the config")(
        "output,o", po::value<string>()->default_value(""),
        "pathname for output")(
        "fin-output", po::value<string>()->default_value(""),
        "pathname for output values when tasks are completed")(
        "total-output", po::value<string>()->default_value(""),
        "pathname for total output values")(
        "times-output", po::value<string>()->default_value(""),
        "pathname for times output")(
        "rundir", po::value<string>()->default_value("run"),
        "directory for creating the directories where the applications are "
        "gonna be executed")(
        "id", po::value<string>()->default_value(random_string(10)),
        "identifier for the experiment")(
        "ti", po::value<double>(),
        "time-interval, duration in seconds of the time interval to sample "
        "performance counters.")("mi", po::value<uint32_t>(),
                                 "max-intervals, maximum number of intervals.")(
        "event,e", po::value<vector<string>>()->composing()->multitoken(),
        "optional list of custom events to monitor (up to 4)")(
        "cpu-affinity", po::value<vector<uint32_t>>()->multitoken(),
        "cpus in which this application (not the workloads) is allowed to run")(
        "clog-min", po::value<string>()->default_value(min_clog),
        "Minimum severity level to log into the console, defaults to warning")(
        "flog-min", po::value<string>()->default_value(min_flog),
        "Minimum severity level to log into the log file, defaults to info")(
        "log-file", po::value<string>()->default_value("manager.log"),
        "file used for the general application log")(
        "monitor-only", po::value<bool>()->default_value("false"),
        "flag that when set to true, only monitors VMs indicated");

    bool option_error = false;
    string option_str;
    po::variables_map vm;
    try {
        // Parse the options without storing them in a map.
        po::parsed_options parsed_options =
            po::command_line_parser(argc, argv).options(desc).run();
        option_str = program_options_to_string(parsed_options.options);

        po::store(parsed_options, vm);
        po::notify(vm);
    } catch (const std::exception &e) {
        cerr << e.what() << endl;
        option_error = true;
    }

    if (vm.count("help") || option_error) {
        cout << desc << endl;
        exit(option_error ? EXIT_FAILURE : EXIT_SUCCESS);
    }

    bool monitor_only = vm["monitor-only"].as<bool>();

    // Log init with user defined parameters
    general_log::init(vm["log-file"].as<string>(),
                      general_log::severity_level(vm["clog-min"].as<string>()),
                      general_log::severity_level(vm["flog-min"].as<string>()));

    // Log the program options
    string cmdline;
    for (int i = 0; i < argc; i++)
        cmdline += " {}"_format(argv[i]);
    LOGINF("Program cmdline:{}"_format(cmdline));
    LOGINF("Program options:\n" + option_str);

    // Open output streams
    auto int_out = std::shared_ptr<std::ostream>();
    auto times_out = std::shared_ptr<std::ostream>();
    auto ucompl_out = std::shared_ptr<std::ostream>();
    auto total_out = std::shared_ptr<std::ostream>();
    open_output_streams(
        vm["output"].as<string>(), vm["fin-output"].as<string>(),
        vm["total-output"].as<string>(), vm["times-output"].as<string>(),
        int_out, ucompl_out, total_out, times_out);

    // Read config
    auto tasklist = tasklist_t();
    auto coslist = vector<Cos>();
    CAT_ptr_t cat;
    auto perf = Perf();
    auto catpol = std::make_shared<
        cat::policy::
            Base>(); // We want to use polimorfism, so we need a pointer
    string config_file;
    try {
        // Read config and set tasklist and coslist
        config_file = vm["config"].as<string>();
        string config_override = vm["config-override"].as<string>();
        config_read(config_file, config_override, options, tasklist, coslist,
                    catpol);

        //Set rundirs for AppTasks
        std::string rundir_base =
            vm["rundir"].as<string>() + "/" + vm["id"].as<string>();
        for (const auto &task_ptr : tasklist) {
            std::shared_ptr<AppTask> app_ptr =
                std::dynamic_pointer_cast<AppTask>(task_ptr);
            if (std::dynamic_pointer_cast<AppTask>(task_ptr) != nullptr) {
                app_ptr->rundir = rundir_base + "/" +
                                  std::to_string(app_ptr->id) + "-" +
                                  app_ptr->name;
                if (fs::exists(app_ptr->rundir))
                    throw_with_trace(std::runtime_error(
                        "The rundir '" + app_ptr->rundir + "' already exists"));
            }
        }
    } catch (const YAML::ParserException &e) {
        LOGFAT(string("Error in config file in line: ") +
               to_string(e.mark.line) + " col: " + to_string(e.mark.column) +
               " pos: " + to_string(e.mark.pos) + ": " + e.msg);
    } catch (const std::exception &e) {
        const auto st = boost::get_error_info<traced>(e);
        if (st)
            LOGFAT(
                "Error reading config file '" + config_file + "': " + e.what()
                << std::endl
                << *st);
        else
            LOGFAT("Error reading config file '" + config_file +
                   "': " + e.what());
    }

    // This options can be setted from the config file
    // The priority order is: commandline > config file > option defaults
    if (!vm["ti"].empty())
        options.ti = vm["ti"].as<double>();

    if (!vm["mi"].empty())
        options.mi = vm["mi"].as<uint32_t>();

    if (!vm["event"].empty())
        options.event = vm["event"].as<vector<string>>();

    if (!vm["cpu-affinity"].empty())
        options.cpu_affinity = vm["cpu-affinity"].as<vector<uint32_t>>();

    // Set Perf type
    perf.set_perf_type(options.perf);

    // Set CPU affinity for not interfering with the executed workloads
    set_cpu_affinity(options.cpu_affinity);

    try {
        // Initial CAT configuration. It may be modified by the CAT policy.
        cat = cat_setup(coslist);
        catpol->set_cat(cat);
    } catch (const std::exception &e) {
        const auto st = boost::get_error_info<traced>(e);
        if (st)
            LOGFAT(e.what() << std::endl << *st);
        else
            LOGFAT(e.what());
    }

    try {
        LOGINF("Leaving tasks ready to execute");
        for (const auto &task_ptr : tasklist) {
            // Leave tasks ready to execute
            task_ptr->task_get_ready_to_execute(monitor_only);
            task_ptr->client_started = false;

            // Add CPU to task if not specified
            if (task_ptr->cpus.size() == 0) {
                uint32_t cpu_id = task_ptr->get_cpu_id(task_ptr->pids[0]);
                LOGINF("Task ID {} is in CPU {} --> update cpus vector"_format(
                    task_ptr->id, cpu_id));
                task_ptr->cpus.push_back(cpu_id);
            }

            // Map task to initial CLOS if specified
            if (task_ptr->initial_clos) {
                for (uint32_t i = 0; i < task_ptr->cpus.size(); i++) {
					if (options.perf == "PID") {
						cat->add_task(task_ptr->initial_clos, task_ptr->pids[i]);
                    	LOGINF("Task PID {} mapped to CLOS {}"_format(
                        	task_ptr->pids[i], task_ptr->initial_clos));
					} else if (options.perf == "CPU") {
						cat->add_cpu(task_ptr->initial_clos, task_ptr->cpus[i]);
                    	LOGINF("Core {} mapped to CLOS {}"_format(
                        	task_ptr->cpus[i], task_ptr->initial_clos));
					}
                }
            }
        }

        LOGINF("***** TASKS READY TO START *****");
        for (const auto &task_ptr : tasklist) {
            std::shared_ptr<VMTask> vm_ptr =
                std::dynamic_pointer_cast<VMTask>(task_ptr);

            int num_cpu = 0;
            for (auto it = task_ptr->cpus.begin(); it != task_ptr->cpus.end();
                 ++it, ++num_cpu) {
                LOGINF("{}: {}"_format(task_ptr->pids[num_cpu], *it));
                if (task_ptr->pids[num_cpu] > 0) {
                    LOGINF("CORE: {}"_format(*it));
                    if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr)
                        LOGINF("Domain: {} Pid: {}"_format(
                            vm_ptr->domain_name, vm_ptr->pids[num_cpu]));

                    // PQOS & Perf monitored events
                    if (options.perf == "CPU") {
                        perf.setup_events(*it, options.event);
                        cat->monitor_setup_core(*it);
                    } else if (options.perf == "PID") {
                        perf.setup_events(task_ptr->pids[num_cpu],
                                          options.event);
                        cat->monitor_setup_pid(task_ptr->pids[num_cpu]);
                    }
                }
            }

            if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                // Set disk I/O limits
                vm_ptr->diskUtils.apply_disk_util_limits(vm_ptr->dom);
            }
        }

        if (!monitor_only) {
            // Start to execute tasks
            LOGINF("Start to execute tasks");
            for (const auto &task_ptr : tasklist) {
                task_ptr->task_start_to_execute();
            }
        }

        // Get network interface (only VMTask)
        for (const auto &task_ptr : tasklist) {
            std::shared_ptr<VMTask> vm_ptr =
                std::dynamic_pointer_cast<VMTask>(task_ptr);
            if (std::dynamic_pointer_cast<VMTask>(task_ptr) != nullptr) {
                VMTask &task = *vm_ptr;
                net_getBridgeInterface(task);
            }
        }

        // Start doing things
        LOGINF("Start main loop");
        if (setjmp(return_to_top_level) == 0)
            simple_loop(tasklist, catpol, perf, options.event,
                        options.ti * 1000 * 1000, options.mi, *int_out,
                        *ucompl_out, *total_out, *times_out, monitor_only);
        else
            clean_and_die(tasklist, catpol->get_cat(), perf, monitor_only);
        // Leaving consistent state after throwing signal
        //int val = setjmp (return_to_top_level);
        //LOGWAR("val = {}"_format(val));
        //if (val)
        //	clean_and_die(tasklist, catpol->get_cat(), perf);

        LOGINF("^^^^^ LOOP FINISHED ^^^^^^");

        // Kill tasks, reset CAT, performance monitors, etc...
        clean(tasklist, catpol->get_cat(), perf);

        // If no --fin-output argument, then the final stats are buffered in a stringstream and then outputted to stdout.
        // If we don't do this and the normal output also goes to stdout, they would mix.
        if (vm["fin-output"].as<string>() == "") {
            auto o = ucompl_out.get();
            cout << dynamic_cast<std::stringstream *>(o)->str();
        }
        // Same...
        if (vm["total-output"].as<string>() == "") {
            auto o = total_out.get();
            cout << dynamic_cast<std::stringstream *>(o)->str();
        }
    } catch (const std::exception &e) {
        const auto st = boost::get_error_info<traced>(e);
        if (st)
            LOGERR(e.what() << std::endl << *st);
        else
            LOGERR(e.what());
        clean_and_die(tasklist, catpol->get_cat(), perf, monitor_only);
    }
}
