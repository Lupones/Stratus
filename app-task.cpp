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


#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <vector>

#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <boost/filesystem.hpp>
#include <cxx-prettyprint/prettyprint.hpp>
#include <fmt/format.h>
#include <glib.h>

#include "app-task.hpp"
#include "log.hpp"
#include "throw-with-trace.hpp"

namespace acc = boost::accumulators;
namespace fs = boost::filesystem;

using std::cerr;
using std::endl;
using std::string;
using std::to_string;
using fmt::literals::operator""_format;

void AppTask::reset()
{
    for (uint32_t i = 0; i < cpus.size(); i++) {
        stats[i].reset_counters();
        set_status(Status::runnable);
    }
}

void AppTask::task_pause()
{
    for (uint32_t i = 0; i < cpus.size(); i++) {
        pid_t pid = pids[i];
        int statusTask = 0;

        if (pid <= 1)
            throw_with_trace(
                std::runtime_error("Tried to send SIGSTOP to pid " +
                                   to_string(pid) + ", check for bugs"));

        kill(pid, SIGSTOP);                              // Stop child process
        if (waitpid(pid, &statusTask, WUNTRACED) != pid) // Wait until it stops
            throw_with_trace(std::runtime_error(
                "Error in waitpid for command '{}' with pid {}"_format(name,
                                                                       pid)));

        if (WIFEXITED(statusTask))
            throw_with_trace(std::runtime_error(
                "Command '" + cmd + "' with pid " + to_string(pid) +
                " exited unexpectedly with status " +
                to_string(WEXITSTATUS(statusTask))));
    }
}

void AppTask::task_resume()
{
    for (uint32_t i = 0; i < cpus.size(); i++) {
        id_t pid = pids[i];
        int statusTask;

        if (pid <= 1)
            throw_with_trace(std::runtime_error(
                "Task {}:{}: tried to send SIGCONT to pid {}, check for bugs"_format(
                    id, name, pid)));

        kill(pid, SIGCONT); // Resume process

        if (waitpid(pid, &statusTask, WCONTINUED) != pid) // Ensure it resumed
            throw_with_trace(std::runtime_error(
                "Error in waitpid for command '{}' with pid {}"_format(name,
                                                                       pid)));

        if (WIFEXITED(statusTask))
            throw_with_trace(std::runtime_error(
                "Command '" + cmd + "' with pid " + to_string(pid) +
                " exited unexpectedly with status " +
                to_string(WEXITSTATUS(statusTask))));
    }
}

void AppTask::task_kill()
{
    for (uint32_t i = 0; i < cpus.size(); i++) {
        id_t pid = pids[i];
        LOGINF("Killing task {}:{}"_format(pids[i], name));

        if (pid > 1) {
            //Already dead
            if (Task::get_status() == Task::Status::exited)
                LOGINF("Task {}:{} with pid {} was already dead"_format(
                    id, name, pid));
            else {
                if (kill(-pid, SIGKILL) < 0)
                    throw_with_trace(std::runtime_error(
                        "Could not SIGKILL command '" + cmd + "' with pid " +
                        to_string(pid) + ": " + strerror(errno)));
            }
            pids[i] = 0;
        } else {
            throw_with_trace(std::runtime_error(
                "Tried to kill pid " + to_string(pid) + ", check for bugs"));
        }
    }
}

void AppTask::task_restart()
{
    LOGINF("Restarting task {}:{} {}/{}"_format(
        id, name, num_restarts + 1,
        max_restarts == std::numeric_limits<decltype(max_restarts)>::max()
            ? "inf"
            : std::to_string(max_restarts)));
    assert(Task::get_status() == Task::Status::limit_reached ||
           Task::get_status() == Task::Status::exited);
    AppTask::reset();
    AppTask::task_remove_rundir();
    AppTask::task_get_ready_to_execute(false);
    AppTask::task_start_to_execute();
    num_restarts++;
}

bool AppTask::task_exited(bool monitor_only) const
{
    if (monitor_only)
        return false;
    int statusTask = 0;
    int ret = waitpid(pids[0], &statusTask, WNOHANG);
    switch (ret) {
        case 0:
            return false;
        case -1:
            throw_with_trace(std::runtime_error(
                "Task {} ({}) with pid {}: error in waitpid"_format(id, name,
                                                                    pids[0])));
        default:
            if (ret != pids[0])
                throw_with_trace(std::runtime_error(
                    "Task {} ({}) with pid {}: strange error in waitpid"_format(
                        id, name, pids[0])));
            break;
    }

    if (WIFEXITED(statusTask)) {
        if (WEXITSTATUS(statusTask) != 0)
            throw_with_trace(std::runtime_error(
                "Task {} ({}) with pid {} exited unexpectedly with status {}"_format(
                    id, name, pids[0], WEXITSTATUS(statusTask))));
        return true;
    }
    return false;
}

void AppTask::task_get_ready_to_execute(bool monitor_only)
{
    int argc;
    char **argv;

    if (!g_shell_parse_argv(cmd.c_str(), &argc, &argv, NULL))
        throw_with_trace(
            std::runtime_error("Could not parse commandline '" + cmd + "'"));

    LOGDEB("Task cpu affinity: " << cpus);

    pid_t pid = fork();

    switch (pid) {
        // Child
        case 0:
            setsid();

            // Set CPU affinity
            try {
                set_cpu_affinity(cpus);
            } catch (const std::exception &e) {
                cerr << "Could not set cpu affinity for task {}:{}: {}"_format(
                            id, name, e.what())
                     << endl;
                exit(EXIT_FAILURE);
            }

            // Drop sudo privileges
            try {
                drop_privileges();
            } catch (const std::exception &e) {
                cerr << "Failed to drop privileges: " + string(e.what())
                     << endl;
            }

            // Create rundir with the necessary files and cd into it
            try {
                task_create_rundir();
            } catch (const std::exception &e) {
                cerr << "Could not create rundir: " + string(e.what()) << endl;
                exit(EXIT_FAILURE);
            }
            fs::current_path(rundir);

            // Redirect OUT/IN/ERR
            if (in != "") {
                fclose(stdin);
                if (fopen(in.c_str(), "r") == NULL) {
                    cerr << "Failed to start program '" + cmd +
                                "', could not open " + in
                         << endl;
                    exit(EXIT_FAILURE);
                }
            }
            if (out != "") {
                fclose(stdout);
                if (fopen(out.c_str(), "w") == NULL) {
                    cerr << "Failed to start program '" + cmd +
                                "', could not open " + out
                         << endl;
                    exit(EXIT_FAILURE);
                }
            }
            if (err != "") {
                fclose(stderr);
                if (fopen(err.c_str(), "w") == NULL) {
                    cerr << "Failed to start program '" + cmd +
                                "', could not open " + err
                         << endl;
                    exit(EXIT_FAILURE);
                }
            }

            // Exec
            execvp(argv[0], argv);

            // Should not reach this
            cerr << "Failed to start program '" + cmd + "'" << endl;
            exit(EXIT_FAILURE);

            // Error
        case -1:
            throw_with_trace(
                std::runtime_error("Failed to start program '" + cmd + "'"));

            // Parent
        default:
            usleep(100); // Wait a bit, just in case
            pids[0] = pid;
            LOGINF(
                "Task {}:{} with pid {} has started"_format(id, name, pids[0]));
            task_pause();
            g_strfreev(argv); // Free the memory allocated for argv
            break;
    }
}

void AppTask::task_start_to_execute()
{
    AppTask::task_resume();
}

void AppTask::task_restart_or_set_done(std::shared_ptr<IntelRDT> cat,
                                       Perf &perf,
                                       const std::vector<std::string> &events)
{
    const auto statusTask = Task::get_status();

    int num_cpu = 0;
    for (auto it = cpus.begin(); it != cpus.end(); ++it) {
        if (statusTask == Task::Status::limit_reached ||
            statusTask == Task::Status::exited) {
            // Stop Perf and Intel monitoring
            if (perf.get_perf_type() == "PID") {
                perf.clean(pids[num_cpu]);
				cat->monitor_stop_pid(pids[num_cpu]);
			} else if (perf.get_perf_type() == "CPU") {
                perf.clean(*it);
            	cat->monitor_stop_core(*it);
			}

            if (statusTask == Task::Status::limit_reached) {
                LOGINF("Task {}:{} limit reached, killing"_format(pids[num_cpu],
                                                                  name));
                AppTask::task_kill();
            } else if (statusTask != Task::Status::exited) {
                throw_with_trace(
                    std::runtime_error("Should not have reached this..."));
            }

            // Restart task if the maximum number of restarts has not been reached
            if (num_restarts < max_restarts) {
                if (cat) {
                    LOGDEB(
                        "Task {}:{} was in CLOS {}, ensure it still is after restart"_format(
                            pids[num_cpu], name, initial_clos));
                    assert(initial_clos < cat->get_max_closids() &&
                           initial_clos >= 0);
                    AppTask::task_restart();
					if (perf.get_perf_type() == "PID") {
                    	cat->add_task(initial_clos, pids[num_cpu]);
                    	cat->monitor_setup_pid(pids[num_cpu]);
					} else if (perf.get_perf_type() == "CPU") {
                    	cat->add_cpu(initial_clos, *it);
                    	cat->monitor_setup_core(*it);
					}
                } else {
                    AppTask::task_restart();
                }

                if (perf.get_perf_type() == "PID")
                    perf.setup_events((int)pids[num_cpu], events);
                else if (perf.get_perf_type() == "CPU")
                    perf.setup_events(*it, events);

            } else {
                Task::set_status(Task::Status::done);
            }
        }

        num_cpu++;
    }
}

void AppTask::task_stats_print_headers(std::ostream &out_stream,
                                       const std::string &sep)
{
    out_stream << "interval" << sep;
    out_stream << "app" << sep;
    out_stream << "CPU" << sep;
    out_stream << "total_CPU%" << sep;
    out_stream << "compl" << sep;
    out_stream << stats[0].header_to_string(sep);
    out_stream << std::endl;
}
void AppTask::task_stats_print_interval(uint64_t interval,
                                        std::ostream &out_stream,
                                        bool monitor_only,
                                        const std::string &sep)
{
	    for (uint32_t i = 0; i < cpus.size(); i++) {
        out_stream << interval << sep << std::setfill('0') << std::setw(2);
        out_stream << id << "_" << name << sep << cpus[i] << sep
                   << Task::total_cpu_util.at(cpus[i]) << sep;

        double completed_inst =
            max_instr
                ? (double)stats[i].sum("inst_retired.any") / (double)max_instr
                : NAN;
        out_stream << completed_inst << sep;
        out_stream << stats[i].data_to_string_int(sep);
        out_stream << std::endl;
    }
}

void AppTask::task_stats_print_times_headers(std::ostream &out_stream,
                                             const std::string &sep)
{
    out_stream << "interval" << sep;
    out_stream << "app" << sep;
    out_stream << "CPU" << sep;
    out_stream << "total_CPU%" << sep;

    std::array<std::string, 10> times = {
        "user", "nice",    "system", "idle",  "iowait",
        "irq",  "softirq", "steal",  "guest", "guest_nice"};
    for (const auto &time : times)
        out_stream << time + "%" << sep;

    out_stream << std::endl;
}

void AppTask::task_stats_print_times_interval(uint64_t interval,
                                              std::ostream &out_stream,
                                              bool monitor_only,
                                              const std::string &sep)
{
	for (uint32_t i = 0; i < cpus.size(); i++) {
        out_stream << interval << sep << std::setfill('0') << std::setw(2);
        out_stream << id << "_" << name << sep << cpus[i] << sep
                   << Task::total_cpu_util.at(cpus[i]) << sep;

        std::array<std::string, 10> times = {
            "user", "nice",    "system", "idle",  "iowait",
            "irq",  "softirq", "steal",  "guest", "guest_nice"};
        for (const auto &time : times)
            out_stream << Task::total_time_util[{time, cpus[i]}] << sep;
        out_stream << std::endl;
    }
}

void AppTask::task_stats_print_total(uint64_t interval,
                                     std::ostream &out_stream,
                                     const std::string &sep)
{
    uint32_t num_cpu = 0;
    for (auto it = cpus.begin(); it != cpus.end(); ++it) {
        out_stream << interval << sep << std::setfill('0') << std::setw(2);
        out_stream << id << "_" << name << sep << *it << sep
                   << Task::total_cpu_util.at(*it) << sep;

        if (num_cpu < cpus.size()) {
            double completed_inst =
                max_instr ? (double)stats[num_cpu].sum("inst_retired.any") /
                                (double)max_instr
                          : NAN;
            out_stream << completed_inst << sep;
            out_stream << stats[num_cpu].data_to_string_total(sep);
            out_stream << std::endl;
        }
        num_cpu++;
    }
}

void AppTask::task_create_rundir()
{
    if (!fs::create_directories(rundir))
        throw_with_trace(
            std::runtime_error("Could not create rundir directory " + rundir));

    // Copy to the rundir the contents of all the skel dirs
    for (const auto &skelItem : skel) {
        if (skelItem != "")
            dir_copy_contents(skelItem, rundir);
    }
}

void AppTask::task_remove_rundir()
{
    fs::remove_all(rundir);
}

int AppTask::get_cpu_id(pid_t pid)
{
    std::ifstream fileStat("/proc/{}/stat"_format(pid));
    std::string line;
    int i = 0;
    int cpu_id = -1;

    while (std::getline(fileStat, line, ' ')) {
        if (i < 38) {
            i++;
        } else {
            cpu_id = stoi(line);
            break;
        }
    }

    return cpu_id;
}

