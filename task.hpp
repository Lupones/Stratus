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

#include <atomic>
#include <map>
#include <vector>

#include "USER_DEFINE.hpp"
#include "common.hpp"
#include "intel-rdt.hpp"
#include "stats.hpp"

class Task
{
    // Number of tasks created
    static std::atomic<uint32_t> ID;

  public:
    typedef std::shared_ptr<Task> task_ptr_t;
    typedef std::vector<task_ptr_t> tasklist_t;

    //Task() = default;
    virtual ~Task();

    enum class Status {
        runnable,
        limit_reached,
        exited,
        done,
    };

    // Set on construction
    const uint32_t id;
    const std::string name;
    pid_t pids[32];  // Set by the task_get_pid function
    Stats stats[32]; // Stats for each PID

    std::vector<uint32_t> cpus;  // Allowed cpus
    const uint32_t initial_clos; // The CLOS this app starts mapped to
    const std::string out;       // Stdout redirection
    const std::string in;        // Stdin redirection
    const std::string err;       // Stderr redirection
    const uint32_t
        max_restarts; // Maximum number of times this application is gonna be restarted after reaching the instruction limit or finishing
    const bool
        batch; // Batch tasks do not need to be completed in order to finish the execution
    const bool client;
    int interval_start = -1;

    bool client_started;

    uint32_t num_restarts =
        0; // Number of times it has reached the instruction limit
    uint32_t completed =
        0; // Number of times it has reached the instruction limit

    int run_id = 0; // To identify the run of the application when it is relaunched serveral times until the completion of a multiprogram workload

	double llc_occup = 0;
	double lmem_bw = 0;
	double tmem_bw = 0;
	double rmem_bw = 0;

    std::map<uint32_t, float> total_cpu_util; //Total CPU utilization of each assigned cpu
	std::map<std::pair<std::string,uint32_t>, float> total_time_util; //Total TIME utilization of each assigned cpu

    Task(const std::string &_name, const std::vector<uint32_t> &_cpus,
         uint32_t _initial_clos, const std::string &_out,
         const std::string &_in, const std::string &_err,
         uint32_t _max_restarts, bool _batch, const bool _client)
        : id(ID++), name(_name), cpus(_cpus), initial_clos(_initial_clos),
          out(_out), in(_in), err(_err), max_restarts(_max_restarts),
          batch(_batch), client(_client)
    {
    }

    static const std::string status_to_str(const Status &s);
    const std::string status_to_str() const;
    const Status &get_status() const;
    void set_status(const Status &new_status);

    // Task management methods
    virtual void reset() = 0;
    virtual void task_pause() = 0;
    virtual void task_resume() = 0;
    virtual void task_kill() = 0;
    virtual void task_get_ready_to_execute(bool monitor_only) = 0;
    virtual void task_start_to_execute() = 0;
    virtual void task_restart() = 0;
    virtual bool task_exited(bool monitor_only) const = 0;
    virtual int get_cpu_id(pid_t pid) = 0;

    // If the limit has been reached, kill the application.
    // If the limit of restarts has not been reached, restart the application. If the limit of restarts was reached, mark the application as done.
    virtual void
    task_restart_or_set_done(std::shared_ptr<IntelRDT> cat, Perf &perf,
                             const std::vector<std::string> &events) = 0;

    // Stats printing to CSV file
    virtual void task_stats_print_headers(std::ostream &out,const std::string &sep = ",") = 0;
	virtual void task_stats_print_times_headers(std::ostream &out,const std::string &sep = ",") = 0;

	virtual void task_stats_print_interval(uint64_t interval, std::ostream &out, bool monitor_only, const std::string &sep = ",") = 0;
    virtual void task_stats_print_total(uint64_t interval, std::ostream &out, const std::string &sep = ",") = 0;
    virtual void task_stats_print_times_interval(uint64_t interval, std::ostream &out_stream, bool monitor_only, const std::string &sep = ",") = 0;

  private:
    Status status = Status::runnable;
};

