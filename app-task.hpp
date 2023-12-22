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

#include "task.hpp"

class AppTask : public Task
{
  public:
    const std::string cmd;
    std::string rundir = ""; // Set before executing the task
    const std::vector<std::string>
        skel; // Directories containing files and folders to copy to rundir
    const uint64_t max_instr; // Max number of instructions to execute

    // Constructor with parameters declared in template.mako
    AppTask(const std::string &_name, const std::vector<uint32_t> &_cpus,
            uint32_t _initial_clos, const std::string &_out,
            const std::string &_in, const std::string &_err,
            uint32_t _max_restarts, bool _batch, const bool _client,
            const std::string &_cmd, const std::vector<std::string> &_skel,
            uint64_t _max_instr)
        : Task(_name, _cpus, _initial_clos, _out, _in, _err, _max_restarts,
               _batch, _client),
          cmd(_cmd), skel(_skel), max_instr(_max_instr)
    {
    }

    /** Override methods **/
    // Task management
    void reset() override;
    void task_pause() override;
    void task_resume() override;
    void task_kill() override;
    void task_restart() override;
    bool task_exited(
        bool monitor_only) const override; // Test if the task has exited
    void task_get_ready_to_execute(bool monitor_only) override;
    void task_start_to_execute() override;
    int get_cpu_id(pid_t pid) override;
    void
    task_restart_or_set_done(std::shared_ptr<IntelRDT> cat, Perf &perf,
                             const std::vector<std::string> &events) override;
    // Stats
    void task_stats_print_headers(std::ostream &out_stream,
                                  const std::string &sep = ",") override;
    void task_stats_print_times_headers(std::ostream &out_stream,
                                        const std::string &sep = ",") override;

    void task_stats_print_interval(uint64_t interval, std::ostream &out_stream,
                                   bool monitor_only,
                                   const std::string &sep = ",") override;
    void task_stats_print_total(uint64_t interval, std::ostream &out_stream,
                                const std::string &sep = ",") override;
    void task_stats_print_times_interval(uint64_t interval,
                                         std::ostream &out_stream,
                                         bool monitor_only,
                                         const std::string &sep = ",") override;

    /** App specific methods **/
    void task_create_rundir();
    void task_remove_rundir();
};
typedef std::shared_ptr<AppTask> app_ptr_t;
typedef std::vector<app_ptr_t> applist_t;

