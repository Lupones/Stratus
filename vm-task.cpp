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
#include <stdexcept>
#include <vector>

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <boost/filesystem.hpp>
#include <cxx-prettyprint/prettyprint.hpp>
#include <fmt/format.h>
#include <glib.h>

#include "log.hpp"
#include "throw-with-trace.hpp"
#include "vm-task.hpp"

#include "pstream.h"
#include <bitset>

namespace acc = boost::accumulators;
namespace fs = boost::filesystem;

using std::cerr;
using std::endl;
using std::string;
using std::to_string;
using fmt::literals::operator""_format;

#define STREQ(a, b) (strcmp(a, b) == 0)
#define MAX_TIMES 10

bool replace(std::string &str, const std::string &from, const std::string &to)
{
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

// Pause task
void VMTask::task_pause()
{
    virDomainInfo dom_info;

    if (dom == NULL) {
        // At this point, the domain should be loaded. Thus, we just throw the error if it is not, but do not try to load it
        throw_with_trace(std::runtime_error(
            "ERROR! Domain invalid when trying to pause the VM " + domain_name +
            "."));
    }

    if (virDomainGetInfo(dom, &dom_info) < 0)
        throw_with_trace(std::runtime_error(
            "ERROR! Unable to get domain info when trying to pause the VM " +
            domain_name + "."));

    LOGDEB("Domain state {}"_format(domain_state_to_str(dom_info.state)));

    switch (dom_info.state) {
        case VIR_DOMAIN_NOSTATE:
        case VIR_DOMAIN_RUNNING:
        case VIR_DOMAIN_BLOCKED:
            /* In these states the domain can be suspended */
            LOGDEB("Suspending domain");
            if (virDomainSuspend(dom) < 0)
                throw_with_trace(std::runtime_error(
                    "ERROR! Unable to pause domain " + domain_name));
            break;

        default:
            /* In all other states domain can't be suspended */
            LOGINF(
                "WARNING! Domain is not in a state ({}) where it can be suspended"_format(
                    domain_state_to_str(dom_info.state)));
            break;
    }
}

// Resume a task
void VMTask::task_resume()
{
    virDomainInfo dom_info;

    if (dom == NULL) {
        // At this point, the domain should be loaded. Thus, we just throw the error if it is not, but do not try to load it
        throw_with_trace(std::runtime_error(
            "ERROR! Domain invalid when trying to resume the VM " +
            domain_name + "."));
    }

    if (virDomainGetInfo(dom, &dom_info) < 0)
        throw_with_trace(std::runtime_error(
            "ERROR! Unable to get domain info when trying to resume the VM " +
            domain_name + "."));

    //DEBUG("Domain state %s", domain_state_to_str(dom_info.state));

    switch (dom_info.state) {
        case VIR_DOMAIN_PAUSED:
            /* This is the expected state for the VM */
            LOGDEB("Resuming the domain.");
            if (virDomainResume(dom) < 0)
                throw_with_trace(std::runtime_error(
                    "ERROR! Unable to resume domain " + domain_name));
            break;

        default:
            /* In all other states domain can't be suspended */
            throw_with_trace(
                std::runtime_error("ERROR! Domain is not in a state ( " +
                                   domain_state_to_str(dom_info.state) +
                                   ") where it can be resumed."));
            break;
    }
}

void VMTask::task_get_pid(bool monitor_only)
{
    LOGINF("***** TASK_GET_PID *****");
    //LOGINF("");
    int num = 0;
    std::string line, ssh_command;
    int ret;

    /*LOGINF("1. Get boot PID");
    std::string command = "sudo grep pid /var/run/libvirt/qemu/" + domain_name +
                      ".xml | grep booted | awk -F \"'\" '{print $6}'";
    LOGINF(command);
        redi::ipstream is(command);
        std::string line;
        while (std::getline(is, line)) {
        pids[num] = std::stoi(line);
        num++;
        }*/

    LOGINF("2. Get VCPUs PIDs");
    std::string command = "sudo grep pid /var/run/libvirt/qemu/" + domain_name +
                          ".xml | grep vcpu | awk -F \"'\" '{print $4}'";
    LOGINF(command);
    redi::ipstream is2(command);
    while (std::getline(is2, line)) {
        pids[num] = std::stoi(line);
        num++;
    }

    num = 0;
    for (auto it = cpus.begin(); it != cpus.end(); ++it, ++num) {
        LOGINF("VCPU PID {}: {}"_format(num, pids[num]));

        if (!monitor_only) {
            // Assign real-time (RT) priority to this process
            ssh_command = "chrt -rr -p 99 " + std::to_string(pids[num]);
            LOGINF("--- Setting real-time priority >>>>>>> {}"_format(
                ssh_command));
            ret = system(ssh_command.c_str());
            if (ret)
                throw_with_trace(std::runtime_error(
                    "Error when setting real-time priority. Command issued: {}"_format(
                        ssh_command)));

            LOGINF(
                "--- Pinning PID of VCPU to corresponding physical CPU  >>>>>>> {}"_format(
                    ssh_command));
            command = "sudo taskset -cp " + std::to_string(*it) + " " +
                      std::to_string(pids[num]);
            LOGINF(">>>>> {}"_format(command));
            ret = system(command.c_str());
            if (ret)
                throw_with_trace(std::runtime_error(
                    "Error when pinning task {} to CPU {}. Command issued: {}"_format(
                        pids[num], *it, command)));
        }
    }

    return;
}

void VMTask::set_VM_num_cpus()
{
    // The VM should be configured before with a number of cores higher than num_cpus (they can be disabled if not needed)
    // This can be done with the following command: virsh setvcpus VM_name --maximum num_cpus --config
    // Additional VM configuration: sudo visudo -> VM_USER ALL=(ALL) NOPASSWD: /bin/bash

    LOGINF("***** SET_VM_NUM_CPUS *****");

    // Set the number of vcpus of the VM
    std::string command = "virsh setvcpus " + domain_name + " " +
                          std::to_string(cpus.size()) + " --live";
    LOGINF(
        "--- Setting the number of vcpus of the VM >>>>>>> {}"_format(command));
    int ret = system(command.c_str());
    if (ret) {
        throw_with_trace(std::runtime_error(
            "Error when setting the number of vcpus of the VM. Command issued: {}"_format(
                command)));
    }
    // Activate (make them online) the added cpus
    for (uint32_t n_cpu = 1; n_cpu < cpus.size(); ++n_cpu) {
        command = "ssh -T " + std::string(VM_USER) + "@" + domain_ip +
                  " 'sudo bash -c \"echo 1 > /sys/devices/system/cpu/cpu" +
                  std::to_string(n_cpu) + "/online\"'";
        LOGINF(">>>>> {}"_format(command));
        ret = system(command.c_str());
        if (ret)
            throw_with_trace(std::runtime_error(
                "Error when setting the number of vcpus of the VM. Command issued: {}"_format(
                    command)));
    }
}

void VMTask::set_client_VM_num_cpus()
{
    // The VM should be configured before with a number of cores higher than num_cpus (they can be disabled if not needed)
    // This can be done with the following command: virsh setvcpus VM_name --maximum num_cpus --config
    // Additional VM configuration: sudo visudo -> VM_USER ALL=(ALL) NOPASSWD: /bin/bash

    LOGINF("***** SET_CLIENT_VM_NUM_CPUS *****");

    // Set the number of vcpus of the client VM
    std::string ssh_command =
        "ssh -p " + std::string(CLIENT_PORT) + " " + std::string(USER_DEFINE) +
        "@" + std::string(CLIENT_IP) + " 'LIBVIRT_DEFAULT_URI=qemu:///system virsh setvcpus " +
        client_domain_name + " " + std::to_string(client_cpus.size()) +
        " --live'";
    LOGINF("--- Setting the number of vcpus of the client VM >>>>>>> {}"_format(
        ssh_command));
    int ret = system(ssh_command.c_str());
    if (ret)
        throw_with_trace(std::runtime_error(
            "Error when setting the number of vcpus of the VM. Command issued: {}"_format(
                ssh_command)));

    // Activate (make them online) the added cpus
    for (uint32_t n_cpu = 1; n_cpu < client_cpus.size(); ++n_cpu) {
        ssh_command = "ssh -T " + std::string(VM_USER) + "@" + client_domain_ip +
                      " 'sudo bash -c \"echo 1 > /sys/devices/system/cpu/cpu" +
                      std::to_string(n_cpu) + "/online\"'";
        LOGINF(">>>>> {}"_format(ssh_command));
        ret = system(ssh_command.c_str());
        if (ret)
            throw_with_trace(std::runtime_error(
                "Error when setting the number of vcpus of the client VM. Command issued: {}"_format(
                    ssh_command)));
    }
}

// Launch the VM, load the snapshot, send the benchmark command, and pause the VM
void VMTask::task_get_ready_to_execute(bool monitor_only)
{
    std::string ssh_command;

    // Check if we have the domain. If not, find it.
    if (dom == NULL) {
        VMTask::task_find_domain();

        // Retrieve the domain
        if (dom == NULL)
            throw_with_trace(
                std::runtime_error("UNEXPECTED ERROR. CHECK HERE!"));
    }

    if (!monitor_only) {
        // Check if we have the snpashot. If not, find it.
        if (!ceph_vm) {
            if (snap == NULL)
                task_find_snapshot();
        } else {
            // TODO: check ceph snapshot exists
        }

        // Load the snapshot in a running (1) or paused (0) state
        if (!ceph_vm)
            task_load_snapshot(1);
        else
            task_load_ceph_snapshot();

        // Set CPU affinity
        task_set_cpu_affinity();
        LOGDEB("Task cpu affinity: " << cpus);
    }

    // Setup CPU usage monitoring
    // See how many physical CPUs can we fetch stats for
    if ((max_id = virDomainGetCPUStats(dom, NULL, 0, 0, 0, 0)) < 0) {
        throw_with_trace(std::runtime_error("Unable to get cpu stats"));
    }
    LOGINF("Number of physical CPUs: {}"_format(max_id));

    // How many stats can we get for a CPU?
    if ((nparams = virDomainGetCPUStats(dom, NULL, 0, 0, 1, 0)) < 0) {
        throw_with_trace(std::runtime_error("Unable to get cpu stats"));
    }
    LOGINF("Number of stats for a CPU: {}"_format(nparams));

    // Allocate memory to structures
    //int *ptr;
    now_params =
        (virTypedParameterPtr)calloc(nparams * max_id, sizeof(*now_params));
    if (now_params == NULL) {
        throw_with_trace(
            std::runtime_error("Memory allocation of now_params failed"));
    }
    then_params =
        (virTypedParameterPtr)calloc(nparams * max_id, sizeof(*then_params));
    if (then_params == NULL) {
        throw_with_trace(
            std::runtime_error("Memory allocation of then_params failed"));
    }

    // Server configs
    if (!monitor_only) {
        VMTask::set_VM_num_cpus();
    }

    VMTask::task_get_pid(monitor_only);

    // Setup applications to execute
    // If it is a client-server benchmark,
    // we need to 1) run the server 2) start the client VM
    if ((!monitor_only) && (client)) {
        /****SERVER SIDE****/
        // Send the command to the server through ssh
        ssh_command =
            "ssh " + std::string(VM_USER) + "@" + domain_ip +
            " './run.sh \"/home/" + std::string(VM_USER) + "/server_scripts/run_script_server_2.sh " +
            args +
            " < /dev/null 2&> /home/" + std::string(VM_USER) + "/" + std::string(OUTPUT_FOLDER) + "/server_log_" +
            std::to_string(run_id) +
            ".txt "
            "&\"'";

        const std::string from_tbench_server_port = "_TBENCH_SERVER_PORT_";
        replace(ssh_command, from_tbench_server_port, domain_port);

        const std::string from_tbench_server_args = "_TBENCH_ARGS_";
        replace(ssh_command, from_tbench_server_args, arguments);

        LOGINF("--- SERVER >>>>>>> {}"_format(ssh_command));

        int ret = system(ssh_command.c_str());
        if (ret)
            throw_with_trace(std::runtime_error(
                "Error when starting the execution of the server benchmark. Command issued: {}"_format(
                    ssh_command)));

        /****CLIENT SIDE****/
        // Start client VM
        if (!client_native) {
            ssh_command = "ssh -p " + std::string(CLIENT_PORT) + " " + std::string(USER_DEFINE) +
                          "@" + std::string(CLIENT_IP) + " 'LIBVIRT_DEFAULT_URI=qemu:///system virsh "
                          "snapshot-revert --domain " +
                          client_domain_name + " --snapshotname " +
                          client_snapshot_name + " --running --force'";

            LOGINF("--- CLIENT >>>>>>> {}"_format(ssh_command));
            ret = system(ssh_command.c_str());
            if (ret)
                throw_with_trace(std::runtime_error(
                    "Error when starting the client VM. Command issued: {}"_format(
                        ssh_command)));
        }
    }

    // Client configs for client VM
    if ((!monitor_only) && (client) && (!client_native)) {
        VMTask::set_client_VM_num_cpus();
        VMTask::task_set_cpu_affinity_client();
    }
}

// To be used when relaunching the application (VM is started rolled back to the snapshot, VM setup alrready applied, etc)
void VMTask::task_get_ready_to_execute_light()
{
    std::string ssh_command;
    LOGINF("task_get_ready_to_execute_light");

    // Setup applications to execute
    // If it is a client-server benchmark,
    // we need to 1) run the server 2) start the client VM
    if (client) {
        /****SERVER SIDE****/
        // Send the command to the server through ssh
        ssh_command =
            "ssh " + std::string(VM_USER) + "@" + domain_ip +
            " './run.sh \"/home/" + std::string(VM_USER) + "/server_scripts/run_script_server_2.sh " +
            args +
            " < /dev/null 2&> /home/" + std::string(VM_USER) + "/" + std::string(OUTPUT_FOLDER) + "/server_log_" +
            std::to_string(run_id) +
            ".txt "
            "&\"'";

        const std::string from_tbench_server_port = "_TBENCH_SERVER_PORT_";
        replace(ssh_command, from_tbench_server_port, domain_port);

        const std::string from_tbench_server_args = "_TBENCH_ARGS_";
        replace(ssh_command, from_tbench_server_args, arguments);

        LOGINF("--- SERVER >>>>>>> {}"_format(ssh_command));

        int ret = system(ssh_command.c_str());
        if (ret)
            throw_with_trace(std::runtime_error(
                "Error when starting the execution of the server benchmark. Command issued: {}"_format(
                    ssh_command)));

        sleep(4);
        /****CLIENT SIDE****/
        // Start client VM
        if (!client_native) {
            ssh_command = "ssh -p " + std::string(CLIENT_PORT) + " " + std::string(USER_DEFINE) +
                          "@" + std::string(CLIENT_IP) + " 'LIBVIRT_DEFAULT_URI=qemu:///system virsh "
                          "snapshot-revert --domain " +
                          client_domain_name + " --snapshotname " +
                          client_snapshot_name + " --running --force'";

            LOGINF("--- CLIENT >>>>>>> {}"_format(ssh_command));
            ret = system(ssh_command.c_str());
            if (ret)
                throw_with_trace(std::runtime_error(
                    "Error when starting the client VM. Command issued: {}"_format(
                        ssh_command)));
        }
    }

    // Client configs for client VM
    if ((client) && (!client_native)) {
        VMTask::set_client_VM_num_cpus();
        VMTask::task_set_cpu_affinity_client();
    }
}

// For SPEC benchmarks -> Send the ssh command to the VM
// For tailbench benchmarks -> Send the ssh command to the client (VM or native)
void VMTask::task_start_to_execute()
{
    std::string ssh_command;

    // We should have the domain
    if (dom == NULL)
        throw_with_trace(std::runtime_error(
            "UNEXPECTED ERROR. Domain " + domain_name +
            " not available when running task_start_to_execute!"));

    if (!client) { // SPEC
        // Send the benchmark command through ssh
        ssh_command =
            "ssh " + std::string(VM_USER) + "@" + domain_ip +
            " './run.sh \"/home/" + std::string(VM_USER) + "/server_scripts/run_script_server_2.sh " +
            args +
            " < /dev/null 2&> /home/" + std::string(VM_USER) + "/" + std::string(OUTPUT_FOLDER) + "/server_log_" +
            std::to_string(run_id) +
            ".txt "
            "&\"'";

        const std::string from_tbench_server_args = "_TBENCH_ARGS_";
        replace(ssh_command, from_tbench_server_args, arguments);

        LOGINF("--->>>>>>> {}"_format(ssh_command));
        int ret = system(ssh_command.c_str());
        if (ret)
            throw_with_trace(std::runtime_error(
                "Error when starting the execution of the SPEC benchmark. Command issued: {}"_format(
                    ssh_command)));
    } else {
        // Tailbench
        // Send the command to the client through ssh
        if (client_native) {
            ssh_command =
                "ssh -p " + std::string(CLIENT_PORT) + " " + std::string(USER_DEFINE) +
                "@" + std::string(CLIENT_IP) + " \"/home/client_scripts/run_script_client_native_2.sh " +
                std::string(USER_DEFINE) + " " + domain_name + " " +
                std::to_string(run_id) + " " + client_args +
                " < /dev/null 2&> "
                "/home/dsf_" +
                domain_name + "/client_log_" + std::to_string(run_id) +
                ".txt "
                "&\"";
        } else {
            ssh_command =
                "ssh " + std::string(VM_USER) + "@" + client_domain_ip +
                " './run.sh "
                "\"/home/" + std::string(VM_USER) + "/client_scripts/run_script_client.sh " +
                std::string(USER_DEFINE) + " " + domain_name + " " +
                client_args +
                " < /dev/null 2&> "
                "/home/" + std::string(VM_USER) + "/dsf_" +
                domain_name +
                "/client_log.txt "
                "&\"'";
        }

        // Replace parameters with template values
        const std::string from_tbench_server = "_TBENCH_SERVER_";
        replace(ssh_command, from_tbench_server, domain_ip);

        const std::string from_tbench_server_port = "_TBENCH_SERVER_PORT_";
        replace(ssh_command, from_tbench_server_port, domain_port);

        const std::string from_tbench_client_args = "_TBENCH_CLIENT_ARGS_";
        replace(ssh_command, from_tbench_client_args, client_arguments);

        LOGINF("--- CLIENT >>>>>>> {}"_format(ssh_command));
        int ret = system(ssh_command.c_str());
        if (ret)
            throw_with_trace(std::runtime_error(
                "Error when starting the execution of client VM. Command issued: {}"_format(
                    ssh_command)));
    }
}

// Shutdown a VM
void VMTask::task_kill()
{
    if (dom == NULL) {
        // At this point, the domain should be loaded.
        // Thus, we just throw the error if it is not,
        // but do not try to load it
        throw_with_trace(std::runtime_error(
            "ERROR! Domain invalid when trying to kill the VM " + domain_name +
            "."));
    }

    if (!virDomainShutdown(dom))
        throw_with_trace(
            std::runtime_error("Could not shutdown domain " + domain_name));

    // Free memory for CPU usage structures
    virTypedParamsFree(now_params, now_nparams * max_id);
    virTypedParamsFree(then_params, then_nparams * max_id);
}

// Kill and restart a task
void VMTask::task_restart()
{
    LOGINF("Restarting task {}: {} {}/{}"_format(
        id, name, num_restarts + 1,
        max_restarts == std::numeric_limits<decltype(max_restarts)>::max()
            ? "inf"
            : std::to_string(max_restarts)));
    assert(Task::get_status() == Task::Status::limit_reached ||
           Task::get_status() == Task::Status::exited);
    VMTask::reset();
    VMTask::task_get_ready_to_execute_light();
    VMTask::task_start_to_execute();
    num_restarts++;
}

void VMTask::task_restart_or_set_done(std::shared_ptr<IntelRDT> cat, Perf &perf,
                                      const std::vector<std::string> &events)
{
    LOGINF("task_restart_or_set_done {}"_format(domain_name));

    const auto curr_status = Task::get_status();

    if (curr_status == Task::Status::limit_reached ||
        curr_status == Task::Status::exited) {
        int num_cpu = 0;
        /*for (auto it = cpus.begin(); it != cpus.end(); ++it) {
                // Stop Perf monitoring
                if (perf.get_perf_type() == "PID")
                        perf.clean(pids[num_cpu]);
                else if (perf.get_perf_type() == "CPU")
                        perf.clean(*it);

                // Stop Intel monitoring
                cat->monitor_stop(pids[num_cpu]);
                num_cpu++;
        }

        LOGINF("monitor stoped");*/

        if (curr_status == Task::Status::limit_reached) {
            LOGINF("Task {}:{} limit reached, killing"_format(id, name));
            task_kill();
        } else if (curr_status != Task::Status::exited) {
            LOGINF(
                "Task {}:{} VM exited --> finish!"_format(pids[num_cpu], name));
        }

        // Restart task if the maximum number of restarts has not been reached
        //if ((num_restarts < max_restarts) && (batch)) {
        if ((num_restarts < max_restarts)) {
            LOGINF(
                "Task {}:{} is batch --> RESTART!"_format(pids[num_cpu], name));
            VMTask::task_restart();
        } else {
            VMTask::set_status(Task::Status::done);
        }

        /*num_cpu = 0;
        for (auto it = cpus.begin(); it != cpus.end(); ++it) {
                if (perf.get_perf_type() == "PID")
                        perf.setup_events((int)pids[num_cpu], events);
                else if (perf.get_perf_type() == "CPU")
                        perf.setup_events(*it, events);

                // Map task to initial CLOS if specified
                if (initial_clos) {
                        cat->add_task(initial_clos, pids[num_cpu]);
                        LOGINF("Task {} mapped to CLOS {}"_format(pids[num_cpu],
                                                                                                          initial_clos));
                }

                cat->monitor_setup(pids[num_cpu]);
                num_cpu++;
        }*/
    }

    LOGINF("task_restart_or_set_done.... done");
}

void VMTask::task_stats_print_interval(uint64_t interval,
                                       std::ostream &out_stream,
                                       bool monitor_only,
                                       const std::string &sep)
{
    const auto curr_status = Task::get_status();
    if (curr_status == Task::Status::exited) {
        LOGINF("Task exited before printing ---> no print interval");
    }

    for (uint32_t i = 0; i < cpus.size(); i++) {
        int cpuID = cpus[i];
        if (!monitor_only)
            cpuID = VMTask::get_cpu_id(pids[i]);
        // LOGINF("{} : {}"_format(pids[i], cpuID));
        out_stream << interval << sep << std::setfill('0') << std::setw(2);
        out_stream << id << "_" << name << sep << cpuID << sep
                   << getTemperatureCPU(cpuID) << sep
                   << VMTask::vm_cpu_util.at(cpuID) << sep
                   << VMTask::total_cpu_util.at(cpuID) << sep;

        out_stream << stats[i].data_to_string_int(sep);
        out_stream << std::endl;
    }
}

void VMTask::task_stats_print_total(uint64_t interval, std::ostream &out_stream,
                                    const std::string &sep)
{
    int num_cpu = 0;
    for (auto it = cpus.begin(); it != cpus.end(); ++it) {
        //if (VMTask::total_cpu_util.at(*it) > 0) {
        //LOGINF("Printing total stats of CPU {}..."_format(*it));
        out_stream << interval << sep << std::setfill('0') << std::setw(2);
        out_stream << id << "_" << name << sep << *it << sep
                   << getTemperatureCPU(*it) << sep
                   << VMTask::vm_cpu_util.at(*it) << sep
                   << VMTask::total_cpu_util.at(*it) << sep;

        out_stream << stats[num_cpu].data_to_string_total(sep);
        out_stream << std::endl;
        //}
        num_cpu++;
    }
}

void VMTask::task_stats_print_headers(std::ostream &out_stream,
                                      const std::string &sep)
{
    out_stream << "interval" << sep;
    out_stream << "app" << sep;
    out_stream << "CPU" << sep;
    out_stream << "Temperature" << sep;
    out_stream << "VM_CPU%" << sep;
    out_stream << "total_CPU%" << sep;
    out_stream << stats[0].header_to_string(sep);
    out_stream << std::endl;
}

void VMTask::task_stats_print_times_headers(std::ostream &out_stream,
                                            const std::string &sep)
{
    out_stream << "interval" << sep;
    out_stream << "app" << sep;
    out_stream << "CPU" << sep;
    out_stream << "VM_CPU%" << sep;
    out_stream << "total_CPU%" << sep;

    std::array<std::string, 10> times = {
        "user", "nice",    "system", "idle",  "iowait",
        "irq",  "softirq", "steal",  "guest", "guest_nice"};
    for (auto &time : times)
        out_stream << time << sep;

    out_stream << std::endl;
}

void VMTask::task_stats_print_times_interval(uint64_t interval,
                                             std::ostream &out_stream,
                                             bool monitor_only,
                                             const std::string &sep)
{
    const auto curr_status = Task::get_status();
    if (curr_status == Task::Status::exited) {
        LOGINF("Task exited before printing ---> no print interval");
    }

    for (uint32_t i = 0; i < cpus.size(); i++) {
        int cpuID = cpus[i];
        if (!monitor_only)
            cpuID = VMTask::get_cpu_id(pids[i]);
        //LOGINF("{} : {}"_format(pids[i], cpuID));
        out_stream << interval << sep << std::setfill('0') << std::setw(2);
        out_stream << id << "_" << name << sep << cpuID << sep
                   << VMTask::vm_cpu_util.at(cpuID) << sep
                   << VMTask::total_cpu_util.at(cpuID) << sep;

        const std::array<std::string, 10> times = {
            "user", "nice",    "system", "idle",  "iowait",
            "irq",  "softirq", "steal",  "guest", "guest_nice"};
        for (const auto &time : times)
            out_stream << Task::total_time_util[{time, cpuID}] << sep;
        out_stream << std::endl;
    }
}

// Check if the task has already excited
// Now, it checks if the SERVER_COMPLETED file has been created
// Before, it was checking if the VM was shutdown
bool VMTask::task_exited(bool monitor_only) const
{
    if (monitor_only)
        return false;

    std::string filename =
        "/homenvm/dsf_" + std::string(domain_name) + "/SERVER_COMPLETED";
    bool exists = fs::exists(filename);
    return exists;

    /*
    virDomainInfo dom_info;

    if (dom == NULL)
            throw_with_trace(std::runtime_error(
                    "ERROR! Domain invalid when trying to check if domain" +
                    domain_name + " exited."));

    if (virDomainGetInfo(dom, &dom_info) < 0)
            throw_with_trace(
                    std::runtime_error("ERROR! Could not get domain info for domain" +
                                                       domain_name + " in task exited function."));

    if (dom_info.state == VIR_DOMAIN_SHUTOFF) {
            LOGINF("Domain {} is shutoff"_format(domain_name));
            return true;
    }

    return false;
    */
}

// Remove the SERVER_COMPLETED file that signals server completion
void VMTask::task_clear_exited()
{
    std::string filename =
        "/homenvm/dsf_" + std::string(domain_name) + "/SERVER_COMPLETED";

    bool exists = fs::exists(filename);

    if (exists) {
        try {
            if (!fs::remove(filename))
                LOGERR("***** file {} NOT deleted."_format(filename));
        } catch (const fs::filesystem_error &err2) {
            LOGERR("***** file {} NOT deleted. Error: {}"_format(filename,
                                                                 err2.what()));
        }
    }
}

// Reset flags
void VMTask::reset()
{
    for (uint32_t i = 0; i < cpus.size(); i++) {
        stats[i].reset_counters();
        set_status(Status::runnable);
    }
}

void VMTask::task_find_snapshot()
{
    int i, n;
    virDomainSnapshotPtr *snaps;
    const char *snapshot_name_char = snapshot_name.c_str();

    // I have no idea why, but without virDomainGetInfo there is a domain error when running virDomainListAllSnapshots
    virDomainInfo dom_info;
    if (virDomainGetInfo(dom, &dom_info) < 0)
        throw_with_trace(std::runtime_error(
            "ERROR! Unable to get domain info when trying to find snapshot " +
            snapshot_name + " of VM " + domain_name + "."));

    // Find the snapshot with snapshot_name
    n = virDomainListAllSnapshots(dom, &snaps, 0);
    for (i = 0; i < n; i++) {
        if (strcmp(snapshot_name_char, virDomainSnapshotGetName(snaps[i])) == 0)
            break;
    }

    if (i == n)
        throw_with_trace(std::runtime_error("ERROR! Unable to find snapshot " +
                                            snapshot_name + " of VM " +
                                            domain_name + "."));
    else
        snap = snaps[i]; // This is the  snapshot we were looking for

    // The snaps structure should be explicitly freed by the user
    for (int j = 0; j < n; j++) {
        if (j != i) // we don't want to free our snpapshot
            virDomainSnapshotFree(snaps[j]);
    }
}

void VMTask::task_load_snapshot(bool load_and_run)
{
    int ret;
    if (load_and_run)
        ret = virDomainRevertToSnapshot(snap,
                                        VIR_DOMAIN_SNAPSHOT_REVERT_RUNNING |
                                            VIR_DOMAIN_SNAPSHOT_REVERT_FORCE);
    else
        ret = virDomainRevertToSnapshot(snap,
                                        VIR_DOMAIN_SNAPSHOT_REVERT_PAUSED |
                                            VIR_DOMAIN_SNAPSHOT_REVERT_FORCE);

    if (ret != 0)
        throw_with_trace(std::runtime_error(
            "Error while trying to revert the domain " + domain_name +
            " to snapshot " + snapshot_name + "."));

    LOGINF("Domain {} sucessfully loaded snapshot {}"_format(domain_name,
                                                             snapshot_name));
}

void VMTask::task_load_ceph_snapshot()
{
    virDomainInfo dom_info;
    int ret, times;

    if (dom == NULL) {
        // At this point, the domain should be loaded. Thus, we just throw the error if it is not, but do not try to load it
        throw_with_trace(std::runtime_error(
            "ERROR! Domain invalid when trying to launch Ceph VM " +
            domain_name + "."));
    }

    if (virDomainGetInfo(dom, &dom_info) < 0)
        throw_with_trace(std::runtime_error(
            "ERROR! Unable to get domain info when trying to launch Ceph VM " +
            domain_name + "."));

    /* Change to logdeb */
    LOGINF("Domain state {}"_format(domain_state_to_str(dom_info.state)));

    switch (dom_info.state) {
        case VIR_DOMAIN_SHUTOFF:
        case VIR_DOMAIN_SHUTDOWN:
            break;

        default:
            /* Note: this was logdeb */
            LOGINF("Shutting down domain");
            if (virDomainShutdown(dom) < 0)
                throw_with_trace(std::runtime_error(
                    "ERROR! Unable to shutdown domain " + domain_name));
            sleep(5);
            break;
    }

    // TODO: check the snapshot actually exists
    std::string rollback_command = "rbd snap rollback libvirt-pool/" +
                                   domain_name + "@" + snapshot_name +
                                   " --user libvirt";
    LOGINF("--->>>>>>> {}"_format(rollback_command));
    ret = system(rollback_command.c_str());
    if (ret)
        throw_with_trace(std::runtime_error(
            "Error when rolling back VM. Command issued: {}"_format(
                rollback_command)));

    ret = virDomainCreate(dom);
    if (ret)
        throw_with_trace(std::runtime_error("Error when launching ceph VM."));

    LOGINF("Domain started");

    //TODO: sleeping while starting the VM
    sleep(10);

    /* TODO: checking state might not be the best solution, ping shoud be checked first */

    // TODO TOCHECK PUCHE
    //while (dom_info.state != VIR_DOMAIN_RUNNING && times < MAX_TIMES) {
    while (times < MAX_TIMES) {
        /* ping_comand: Returns 'OK' when the ping works and 'FAIL' otherwise */
        std::string ping_command =
            "ping -qc1 " + domain_ip +
            " 2>&1 | awk -F\'/\' \'END{ print (/^rtt/?\"OK\":\"FAIL\") }\'";

        ret = system((ping_command + " > stdout_temp.txt").c_str());
        LOGINF("--->>>>>>> {}"_format(ping_command));

        if (ret)
            throw_with_trace(
                std::runtime_error("Error when trying to ping ceph VM."));

        std::ifstream ifs("stdout_temp.txt");
        std::string ping_ok{std::istreambuf_iterator<char>(ifs),
                            std::istreambuf_iterator<char>()};
        ifs.close();
        std::remove("stdout_temp.txt");

        if (strcmp(ping_ok.c_str(), "OK") == 0)
            break;

        times++;
        sleep(5);
        /* TODO: return error if we get to times = MAX_TIMES tries */
    }
}

void VMTask::task_find_domain()
{
    if (conn == NULL)
        throw_with_trace(std::runtime_error(
            "ERROR! No valid hypervisor pointer when trying to find domain " +
            domain_name + "."));

    dom = virDomainLookupByName(conn, domain_name.c_str());
    if (!dom)
        throw_with_trace(std::runtime_error("ERROR! Could not find domain " +
                                            domain_name + "."));
}

// Set the affinity of the SERVER VM from a vector of cores
// For now, it maps all VCPUs to the entire vector of cores
void VMTask::task_set_cpu_affinity()
{
    uint32_t vcpu;
    int maplen = 6;
    int desp;
    int pos;
    unsigned char cpumap
        [maplen]; // A bit mask of 6 chars is enough for 48 cores (as xpl4 has)

    for (int i = 0; i < maplen; i++)
        cpumap[i] = 0x0;

    for (auto it = cpus.begin(); it != cpus.end(); ++it) {
        vcpu = *it;
        LOGINF(" ---- CPU {} ----"_format(vcpu));
        pos = (vcpu / 8);
        desp = vcpu % 8;

        if (vcpu > 47)
            throw_with_trace(
                std::runtime_error("ERROR! Max CPU for XPL4 is 47.\n"));

        cpumap[pos] = cpumap[pos] | (1 << desp);
    }

    int i = 0;
    for (auto it = cpus.begin(); it != cpus.end(); ++it, ++i) {
        if (virDomainPinVcpu(dom, i, cpumap, maplen) == -1) {
            throw_with_trace(std::runtime_error("ERROR! Could not pin domain " +
                                                domain_name + " to CPU " +
                                                std::to_string(*it) + "."));
        }
    }
}

// Set the affinity of the CLIENT VM from a vector of cores
// For now, it maps all VCPUs to the entire vector of cores
void VMTask::task_set_cpu_affinity_client()
{
    if (client_cpus.size() == 0) {
        // No list of cpus for the client
        // VCPUs are free to run on any core
        return;
    }

    std::string ssh_command;

    // First, create the cpu_list as a string
    std::string cpu_list;
    /*vcpu = *it;
    cpu_list.append(std::to_string(vcpu));

    for (++it; it != client_cpus.end(); ++it) {
        vcpu = *it;
        cpu_list.append(",");
        cpu_list.append(std::to_string(vcpu));
    }*/

    int i = 0;
    for (auto it = client_cpus.begin(); it != client_cpus.end(); ++it, ++i) {
        ssh_command =
            "ssh -p " + std::string(CLIENT_PORT) + " " + std::string(USER_DEFINE) +
            "@" + std::string(CLIENT_IP) + " 'LIBVIRT_DEFAULT_URI=qemu:///system virsh vcpupin " +
            client_domain_name + " --vcpu " + std::to_string(i) +
            " --cpulist " + std::to_string(*it) + " --live'";

        LOGINF(
            "--- Setting the CPU affinity of the client VM >>>>>>> {}"_format(
                ssh_command));
        int ret = system(ssh_command.c_str());
        if (ret) {
            throw_with_trace(std::runtime_error(
                "Error when setting the CPU affinity of the client VM. Command issued: {}"_format(
                    ssh_command)));
        }
    }
}

std::string VMTask::domain_state_to_str(unsigned char state)
{
    switch (state) {
        case 0:
            return "VIR_DOMAIN_NOSTATE";
        case 1:
            return "VIR_DOMAIN_RUNNING";
        case 2:
            return "VIR_DOMAIN_BLOCKED";
        case 3:
            return "VIR_DOMAIN_PAUSED";
        case 4:
            return "VIR_DOMAIN_SHUTDOWN";
        case 5:
            return "VIR_DOMAIN_SHUTOFF";
        case 6:
            return "VIR_DOMAIN_CRASHED";
        case 7:
            return "VIR_DOMAIN_PMSUSPENDED";
        case 8:
            return "VIR_DOMAIN_LAST";
        default:
            throw_with_trace(std::runtime_error(
                "Unknown virDomainState, should not reach this"));
    }
}

// CPU utilization of physical CPUs
float VMTask::task_get_VM_total_CPU_usage(unsigned long long int then,
                                          unsigned long long int now)
{
    //LOGINF("task_get_VM_total_CPU_usage {}"_format(domain_name));

    int j;
    float usage = 0;

    if (then_nparams != now_nparams) {
        /* this should not happen (TM) */
        throw_with_trace(std::runtime_error("parameters counts don't match"));
    }

    for (auto it = cpus.begin(); it != cpus.end(); ++it) {
        size_t pos;
        float vcpu_util;

        /* check if the vCPU is in the maps */
        if (now_params[*it * now_nparams].type == 0 ||
            then_params[*it * then_nparams].type == 0)
            continue;

        for (j = 0; j < now_nparams; j++) {
            pos = *it * now_nparams + j;
            if (STREQ(then_params[pos].field, VIR_DOMAIN_CPU_STATS_VCPUTIME))
                break;
        }

        if (j == now_nparams) {
            throw_with_trace(std::runtime_error(
                "unable to find VIR_DOMAIN_CPU_STATS_VCPUTIME"));
        }

        try {
            if ((now > then) &
                (now_params[pos].value.ul > then_params[pos].value.ul))
                vcpu_util =
                    (now_params[pos].value.ul - then_params[pos].value.ul) /
                    (now - then) / 10;
            else
                vcpu_util = 0;
        } catch (const std::logic_error e) {
            LOGINF("Erroneous value for vcpu_util... Set to 0");
            vcpu_util = 0;
        }
        //LOGINF("VM CPU {} utilization: {}%"_format(*it, vcpu_util));
        usage = usage + vcpu_util;
    }

    return usage;
}

// CPU utilization of physical CPUs
float VMTask::task_get_VM_CPU_usage(unsigned long long int then,
                                    unsigned long long int now, int cpu)
{
    int j;
    size_t pos;
    float vcpu_util;

    if (then_nparams != now_nparams) {
        /* this should not happen (TM) */
        throw_with_trace(std::runtime_error("parameters counts don't match"));
    }

    /* check if the vCPU is in the maps */
    if (now_params[cpu * now_nparams].type == 0 ||
        then_params[cpu * then_nparams].type == 0)
        throw_with_trace(std::runtime_error("VCPU is not in the maps"));

    for (j = 0; j < now_nparams; j++) {
        pos = cpu * now_nparams + j;
        if (STREQ(then_params[pos].field, VIR_DOMAIN_CPU_STATS_VCPUTIME))
            break;
    }

    if (j == now_nparams) {
        throw_with_trace(
            std::runtime_error("unable to find VIR_DOMAIN_CPU_STATS_VCPUTIME"));
    }

    if ((now > then) & (now_params[pos].value.ul > then_params[pos].value.ul))
        vcpu_util = (now_params[pos].value.ul - then_params[pos].value.ul) /
                    (now - then) / 10;
    else
        vcpu_util = 0;

    return vcpu_util;
}

int VMTask::get_cpu_id(pid_t pid)
{
    std::ifstream fileStat("/proc/{}/stat"_format(pid));
    std::string line;
    int i = 0;
    int cpu_id = -1;

    int LIMIT = 39;
    while (std::getline(fileStat, line, ' ')) {
        if ((i == 1) & (line == "(qemu-system-x86)"))
            LIMIT = 38;

        if (i < LIMIT) {
            i++;
        } else {
            cpu_id = stoi(line);
            break;
        }
    }

    return cpu_id;
}

