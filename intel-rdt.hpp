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

#include "intel-cmt-cat/lib/pqos.h"
#include "intel-cmt-cat/lib/os_monitoring.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * Defines used to identify CAT mask definitions
 */
#define CAT_UPDATE_SCOPE_BOTH 0 /**< update COS code & data masks */
#define CAT_UPDATE_SCOPE_DATA 1 /**< update COS data mask */
#define CAT_UPDATE_SCOPE_CODE 2 /**< update COS code mask */

#define DIM(x) (sizeof(x) / sizeof(x[0]))

/** MONITORING **/
#define PQOS_MAX_PIDS 32
#define PQOS_MAX_CORES 1024

typedef uint64_t cbm_t; // Cache Bitmask
typedef std::vector<cbm_t>
    cbms_t; // Array of CBMs, there should be one per CLOS

class IntelRDT
{
    bool initialized;
    const struct pqos_cpuinfo *p_cpu;
    const struct pqos_cap *p_cap;
    unsigned *p_sockets;
    unsigned sock_count;

    //struct pqos_mon_data *m_mon_grps[PQOS_MAX_PIDS];
    struct pqos_mon_data *m_mon_grps[PQOS_MAX_CORES];
    unsigned num_pids = 0;
    unsigned num_cores = 0;

	bool free_pids = false;
	unsigned free_pids_index = 0;

	bool free_cores = false;
	unsigned free_cores_index = 0;

  public:
    IntelRDT() = default;
    ~IntelRDT() = default;

    bool is_initialized() const;
    void init();
    void reset();
    void fini();

    void set_cbm(uint32_t clos, uint32_t socket, uint64_t cbm, uint32_t cdp,
                 std::string type = "code");
    void add_cpu(uint32_t clos, uint32_t cpu);
    void set_config(const enum pqos_cdp_config l3_cdp_cfg,
                    const enum pqos_mba_config mba_cfg);

    uint32_t get_clos(uint32_t cpu) const;
    uint64_t get_cbm(uint32_t clos, uint32_t socket,
                     std::string type = "code") const;
    uint32_t get_max_closids() const;

    /* CAT Intel API */
    int set_l3_clos(const unsigned clos, const uint64_t mask,
                    const unsigned socket, int cdp, const unsigned scope);
    void add_task(uint32_t clos, pid_t pid);
    uint32_t get_clos_of_task(pid_t pid) const;

    /*MBA Intel API*/
    int set_mba_clos(const unsigned clos, const uint64_t mb,
                     const unsigned socket, int ctrl);
    void set_mb(uint32_t clos, uint32_t socket, int ctrl, unsigned mb);
    uint64_t get_mb(uint32_t clos, uint32_t socket);

    /*Monitoring PID*/
    int monitor_setup_pid(pid_t pid);
    void monitor_get_values_pid(pid_t pid, double *llc_occup, double *lmem_bw,
                                double *tmem_bw, double *rmem_bw);
    int monitor_stop_pid(pid_t pid);

    /*Monitoring core*/
    int monitor_setup_core(uint32_t core);
    void monitor_get_values_core(uint32_t core, double *llc_occup,
                                 double *lmem_bw, double *tmem_bw,
                                 double *rmem_bw);
    int monitor_stop_core(uint32_t core);

    void print();
};
