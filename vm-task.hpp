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
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#ifndef LIBVIRT_HEADERS_H
#define LIBVIRT_HEADERS_H

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#endif

#ifndef DISK_UTILS_H
#define DISK_UTILS_H
#include "disk-utils.hpp"
#endif

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/framework/accumulator_base.hpp>
#include <boost/accumulators/framework/parameters/sample.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/rolling_variance.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/stats.hpp>


namespace acc = boost::accumulators;

enum vm_categories {
    vm_cat_invalid = 0,
    vm_cat_cpu_mem,
	vm_cat_cpu_mem_low,
    vm_cat_disk,
    vm_cat_disk_rd,
    vm_cat_disk_wr,
    vm_cat_network,
    vm_cat_unknown
};


#ifndef WIN_SIZE
#define WIN_SIZE 30
#endif

class VMTask : public Task
{
  public:
    const virConnectPtr
        conn; // The hypervisor pointer should not be part of the task, but it will ease things if it is kept in the task struct
              // It should be passed (already loaded) in the constructor
    const std::string domain_name;
    const std::string domain_ip;
    const std::string domain_port;
    const std::string snapshot_name;
    const bool ceph_vm;
    const bool client_native;
    DiskUtils diskUtils;
    const std::string client_domain_name;
    const std::string client_domain_ip;
    const std::string client_snapshot_name;
    const int client_num_cpus;

    std::vector<uint32_t> client_cpus; // Allowed cpus for the client
    virDomainPtr dom;
    virDomainSnapshotPtr snap;

    char network_interface[20];
    long long read_network_bwtx;
    long long read_network_bwrx;
    float network_bwtx;
    float network_bwrx;
    double ovs_bwtx;
    double ovs_bwrx;

    std::string args;             // Args for the server application
    std::string client_args;      // Args for the client application
    std::string arguments;        // Args for the server application
    std::string client_arguments; // Args for the client application

    long long netbw_in_avg;
    long long netbw_in_peak;
    long long netbw_in_burst;
    long long netbw_out_avg;
    long long netbw_out_peak;
    long long netbw_out_burst;

    long long ceph_op_latency;
    long long ceph_op_in_bytes;
    long long ceph_op_out_bytes;

    std::ofstream cat_out_stream;
    std::ofstream model_out_stream;

    //Parameters to monitor VM utilization of each assigned cpu
    std::map<uint32_t, float> vm_cpu_util;
    int max_id = 0;
    int nparams = 0, then_nparams = 0, now_nparams = 0;
    virTypedParameterPtr then_params = NULL, now_params = NULL;

    std::map<std::string, double> vm_baseline_metrics;

#define ACC boost::accumulators
    typedef ACC::accumulator_set<
        double, ACC::stats<ACC::tag::last, ACC::tag::sum, ACC::tag::mean,
                           ACC::tag::variance, ACC::tag::rolling_mean,
                           ACC::tag::rolling_variance, ACC::tag::rolling_count>>
        accum_t;

    // VM category and its accumulators that gather stats and categorize the workload
    vm_categories vm_cat;
    accum_t acc_cpu;
    accum_t acc_mean_cpu;
    accum_t acc_gips;
    accum_t acc_disk;
    accum_t acc_network;
    accum_t acc_net_tx;
    accum_t acc_net_rx;
    accum_t acc_membw;
    accum_t acc_l1mpki;
    accum_t acc_l2mpki;
    accum_t acc_l3mpki;
    accum_t acc_llcocc;
    accum_t acc_guest;
    accum_t acc_guest_aux;
    accum_t acc_idle;
    accum_t acc_ipc;
    accum_t acc_corebound;
    accum_t acc_membound;
    accum_t acc_diskbw;
    accum_t acc_diskbw_cor;
	accum_t acc_stallsmem;
	accum_t acc_stallscore;
	accum_t acc_stallstot;

#undef ACC

    // Constructor with parameters declared in template.mako
    VMTask(const std::string &_name, const std::vector<uint32_t> &_cpus,
           uint32_t _initial_clos, const std::string &_out,
           const std::string &_in, const std::string &_err,
           uint32_t _max_restarts, bool _batch, const bool _client,
           const virConnectPtr &_conn, const std::string &_domain_name,
           const std::string &_domain_ip, const std::string &_domain_port,
           const std::string &_snapshot_name, const bool _ceph_vm,
           const bool _client_native, const disk_config _dc,
           const std::string &_client_domain_name,
           const std::string &_client_domain_ip,
           const std::string &_client_snapshot_name, const int _client_num_cpus,
           const std::vector<uint32_t> &_client_cpus, std::string _args,
           std::string _client_args, std::string _arguments,
           std::string _client_arguments, long long _netbw_in_avg,
           long long _netbw_in_peak, long long _netbw_in_burst,
           long long _netbw_out_avg, long long _netbw_out_peak,
           long long _netbw_out_burst)
        : Task(_name, _cpus, _initial_clos, _out, _in, _err, _max_restarts,
               _batch, _client),
          conn(_conn), domain_name(_domain_name), domain_ip(_domain_ip),
          domain_port(_domain_port), snapshot_name(_snapshot_name),
          ceph_vm(_ceph_vm), client_native(_client_native), diskUtils(_dc),
          client_domain_name(_client_domain_name),
          client_domain_ip(_client_domain_ip),
          client_snapshot_name(_client_snapshot_name),
          client_num_cpus(_client_num_cpus), client_cpus(_client_cpus),
          args(_args), client_args(_client_args), arguments(_arguments),
          client_arguments(_client_arguments), netbw_in_avg(_netbw_in_avg),
          netbw_in_peak(_netbw_in_peak), netbw_in_burst(_netbw_in_burst),
          netbw_out_avg(_netbw_out_avg), netbw_out_peak(_netbw_out_peak),
          netbw_out_burst(_netbw_out_burst),
          acc_cpu(boost::accumulators::tag::rolling_window::window_size =
                      WIN_SIZE),
          acc_mean_cpu(boost::accumulators::tag::rolling_window::window_size =
                           WIN_SIZE),
          acc_gips(boost::accumulators::tag::rolling_window::window_size =
                       WIN_SIZE),
          acc_disk(boost::accumulators::tag::rolling_window::window_size =
                       WIN_SIZE),
          acc_network(boost::accumulators::tag::rolling_window::window_size =
                          WIN_SIZE),
          acc_net_tx(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_net_rx(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_membw(boost::accumulators::tag::rolling_window::window_size =
                        WIN_SIZE),
          acc_l1mpki(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_l2mpki(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_l3mpki(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_llcocc(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_guest(boost::accumulators::tag::rolling_window::window_size =
                        WIN_SIZE),
          acc_guest_aux(boost::accumulators::tag::rolling_window::window_size =
                            WIN_SIZE),
          acc_idle(boost::accumulators::tag::rolling_window::window_size =
                       WIN_SIZE),
          acc_ipc(boost::accumulators::tag::rolling_window::window_size =
                      WIN_SIZE),
          acc_corebound(boost::accumulators::tag::rolling_window::window_size =
                            WIN_SIZE),
          acc_membound(boost::accumulators::tag::rolling_window::window_size =
                           WIN_SIZE),
          acc_diskbw(boost::accumulators::tag::rolling_window::window_size =
                         WIN_SIZE),
          acc_diskbw_cor(boost::accumulators::tag::rolling_window::window_size =
                             WIN_SIZE),
		  acc_stallsmem(boost::accumulators::tag::rolling_window::window_size =
                             WIN_SIZE),
		  acc_stallscore(boost::accumulators::tag::rolling_window::window_size =
                             WIN_SIZE),
		  acc_stallstot(boost::accumulators::tag::rolling_window::window_size =
                             WIN_SIZE)
    {
        dom = NULL;
        snap = NULL;
        run_id = 0;
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
    void task_clear_exited();              // delete the SERVER_COMPLETED file
    void task_get_ready_to_execute(bool monitor_only) override;
    void task_get_ready_to_execute_light();
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

    /** VM specific methods **/
    void task_find_domain();
    void task_find_snapshot();
    void task_load_snapshot(bool load_and_run);
    void task_load_ceph_snapshot();
    void task_set_cpu_affinity();
    void task_set_cpu_affinity_client();
    std::string domain_state_to_str(unsigned char state);
    void task_get_pid(bool monitor_only);
    void set_VM_num_cpus();
    void set_client_VM_num_cpus();

    // Total CPU utilization of a VM
    float task_get_VM_total_CPU_usage(unsigned long long int then,
                                      unsigned long long int now);
    // CPU utilization of a VM's given core
    float task_get_VM_CPU_usage(unsigned long long int then,
                                unsigned long long int now, int cpu);
};
typedef std::shared_ptr<VMTask> vm_ptr_t;
typedef std::vector<vm_ptr_t> vmlist_t;

