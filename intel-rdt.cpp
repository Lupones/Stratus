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

#include <cassert>
#include <chrono>
#include <cxx-prettyprint/prettyprint.hpp>
#include <fmt/format.h>
#include <iostream>
#include <stdexcept>
#include <sys/time.h>

#include "intel-rdt.hpp"
#include "log.hpp"
#include "throw-with-trace.hpp"

using fmt::literals::operator""_format;

/**
 * Maintains a table of process id, event, number of events that are selected
 * in config string for monitoring LLC occupancy
 */
static struct {
    pid_t pid;
    struct pqos_mon_data *pgrp;
    enum pqos_mon_event events;
} sel_monitor_pid_tab[PQOS_MAX_PIDS];

static struct {
    uint32_t core;
    struct pqos_mon_data *pgrp;
    enum pqos_mon_event events;
} sel_monitor_core_tab[PQOS_MAX_CORES];

bool IntelRDT::is_initialized() const
{
    return initialized;
}

void IntelRDT::init()
{
    int ret;
    struct pqos_config cfg = {};
    // Specify to use OS with resctrl monitoring interface
    cfg.interface = PQOS_INTER_OS_RESCTRL_MON;

    cfg.fd_log = STDERR_FILENO;
    cfg.verbose = 0;

    // PQoS Initialization - Check and initialize CAT and CMT capability
    ret = pqos_init(&cfg);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Could not initialize PQoS library"));

    // Get CMT capability and CPU info pointer
    ret = pqos_cap_get(&p_cap, &p_cpu);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Could not retrieve PQoS capabilities"));

    // Get CPU socket information to set CLOS
    p_sockets = pqos_cpu_get_sockets(p_cpu, &sock_count);
    if (p_sockets == NULL)
        throw_with_trace(
            std::runtime_error("Could not retrieve CPU socket information"));

    // Initialize Perf structures used for OS monitoring interface
    ret = os_mon_init(p_cpu, p_cap);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Could not initialize Pef OS monitoring"));

    initialized = true;
    reset();
}

void IntelRDT::set_config(const enum pqos_cdp_config l3_cdp_cfg,
                          const enum pqos_mba_config mba_cfg)
{
    if (!initialized)
        throw_with_trace(
            std::runtime_error("Could not set CDP and MBA configuration: init "
                               "method must be called first"));

    // Reset configuration of CAT and MBA technologies
    int ret = pqos_alloc_reset(l3_cdp_cfg, PQOS_REQUIRE_CDP_ANY, mba_cfg);

    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("CAT reset returned error code " +
                                            std::to_string(ret)));
}

void IntelRDT::set_cbm(uint32_t clos, uint32_t socket, uint64_t mask,
                       uint32_t cdp, std::string type)
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not set mask: init method must be called first"));

    struct pqos_l3ca l3ca_cos = {};
    l3ca_cos.class_id = clos;
    l3ca_cos.cdp = cdp;

    struct pqos_l3ca l3ca_prev[PQOS_MAX_L3CA_COS];
    uint32_t num_cos;

    if (pqos_l3ca_get(socket, PQOS_MAX_L3CA_COS, &num_cos, l3ca_prev) !=
        PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Could not get mask for CLOS" +
                                            std::to_string(clos)));

    assert(l3ca_prev[clos].class_id == clos);

    // Set mask depending on data or code prio
    if (l3ca_cos.cdp == 1) {
        if (type.compare("code")) {
            l3ca_cos.u.s.code_mask = mask;
            l3ca_cos.u.s.data_mask = l3ca_prev[clos].u.s.data_mask;
        } else {
            l3ca_cos.u.s.data_mask = mask;
            l3ca_cos.u.s.code_mask = l3ca_prev[clos].u.s.code_mask;
        }
    } else
        l3ca_cos.u.ways_mask = mask;

    int ret = pqos_l3ca_set(p_sockets[socket], 1, &l3ca_cos);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Could not set CLOS mask"));
}

void IntelRDT::add_cpu(uint32_t clos, uint32_t cpu)
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not associate cpu: init method must be called first"));

    int ret = pqos_alloc_assoc_set(cpu, clos);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error(
            "Could not associate core with class of service"));
}

uint32_t IntelRDT::get_clos(uint32_t cpu) const
{
    uint32_t clos;
    if (pqos_alloc_assoc_get(cpu, &clos) != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Could not get CLOS for CPU " +
                                            std::to_string(cpu)));
    return clos;
}

uint64_t IntelRDT::get_cbm(uint32_t clos, uint32_t socket,
                           std::string type) const
{
    struct pqos_l3ca l3ca[PQOS_MAX_L3CA_COS];
    uint32_t num_cos;
    uint64_t mask;

    if (pqos_l3ca_get(socket, PQOS_MAX_L3CA_COS, &num_cos, l3ca) !=
        PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Could not get mask for CLOS" +
                                            std::to_string(clos)));

    assert(l3ca[clos].class_id == clos);

    if (l3ca[clos].cdp == 1) {
        if (type.compare("code"))
            mask = l3ca[clos].u.s.code_mask;
        else
            mask = l3ca[clos].u.s.data_mask;
    } else
        mask = l3ca[clos].u.ways_mask;

    return mask;
}

uint32_t IntelRDT::get_max_closids() const
{
    uint32_t max_num_cos;
    if (pqos_l3ca_get_cos_num(p_cap, &max_num_cos) != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Could not get the max number of CLOS"));
    return max_num_cos;
}

void IntelRDT::reset()
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    int ret = pqos_alloc_reset(PQOS_REQUIRE_CDP_ANY, PQOS_REQUIRE_CDP_ANY,
                               PQOS_MBA_ANY);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("PQOS Allocation reset returned error code " +
                               std::to_string(ret)));

    ret = os_mon_reset();
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error(
            "OS monitoring reset returned error code " + std::to_string(ret)));
}

void IntelRDT::fini()
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    int ret;
    ret = os_mon_fini();
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Error shutting down OS monitoring library!"));

    ret = pqos_fini();
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Error shutting down PQoS library!"));
}

/**
 * @brief Set L3 class definitions on selected sockets
 *
 * @param clos L3 CLOS ID to set
 * @param mask class bitmask to set
 * @param sock_num Number of socket ID's in the array
 * @param cdp indicates if CDP is used
 * @param scope L3 CAT update scope i.e. CDP Code/Data
 *
 * @return Number of classes set
 * @retval -1 on error
 */
int IntelRDT::set_l3_clos(const unsigned clos, const uint64_t mask,
                          const unsigned socket, int cdp, const unsigned scope)
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    if (p_sockets == NULL || mask == 0) {
        throw_with_trace(
            std::runtime_error("Failed to set L3 CAT configuration!"));
        return -1;
    }

    // Get previous L3 configuration
    struct pqos_l3ca l3ca_prev[PQOS_MAX_L3CA_COS];
    uint32_t num_cos;

    if (pqos_l3ca_get(socket, PQOS_MAX_L3CA_COS, &num_cos, l3ca_prev) !=
        PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Could not get mask for CLOS" +
                                            std::to_string(clos)));

    assert(l3ca_prev[clos].class_id == clos);

    // Define new L3 configuration
    struct pqos_l3ca l3ca_cos = {};
    l3ca_cos.class_id = clos;
    l3ca_cos.cdp = cdp;

    // Set mask depending on data or code prio
    if (l3ca_cos.cdp == 1) {
        if (scope == CAT_UPDATE_SCOPE_CODE) {
            l3ca_cos.u.s.code_mask = mask;
            l3ca_cos.u.s.data_mask = l3ca_prev[clos].u.s.data_mask;
        } else if (scope == CAT_UPDATE_SCOPE_DATA) {
            l3ca_cos.u.s.data_mask = mask;
            l3ca_cos.u.s.code_mask = l3ca_prev[clos].u.s.code_mask;
        } else if (scope == CAT_UPDATE_SCOPE_BOTH) {
            l3ca_cos.u.s.code_mask = mask;
            l3ca_cos.u.s.data_mask = mask;
        }
    } else
        l3ca_cos.u.ways_mask = mask;

    int ret = pqos_l3ca_set(p_sockets[socket], 1, &l3ca_cos);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Could not set CLOS mask"));

    if (l3ca_cos.cdp)
        LOGINF("SOCKET {} L3CA CLOS {} => DATA 0x{:x},CODE 0x{:x}"_format(
            socket, l3ca_cos.class_id, l3ca_cos.u.s.data_mask,
            l3ca_cos.u.s.code_mask));
    else
        LOGINF("SOCKET {} L3CA CLOS {} => MASK 0x{:x}"_format(
            socket, l3ca_cos.class_id, l3ca_cos.u.ways_mask));

    return 1;
}

void IntelRDT::add_task(uint32_t clos, pid_t pid)
{
    int ret;
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    ret = pqos_alloc_assoc_set_pid(pid, clos);

    assert(ret == PQOS_RETVAL_OK);
    if (ret == PQOS_RETVAL_PARAM) {
        printf("Task ID number or class id is out of bounds!\n");
    } else if (ret != PQOS_RETVAL_OK) {
        printf("Setting allocation class of service association failed!\n");
    }
}

uint32_t IntelRDT::get_clos_of_task(pid_t pid) const
{
    uint32_t clos;
    int ret;
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    ret = pqos_alloc_assoc_get_pid(pid, &clos);

    if (ret != PQOS_RETVAL_OK) {
        throw_with_trace(std::runtime_error("Could not get CLOS of task "));
        return -1;
    }

    return clos;
}

void IntelRDT::print()
{
}

/**
 * @brief Set MBA class definitions on given socket
 *
 * @param clos MBA CLOS ID to set
 * @param mb to set
 * @param socket Socket ID
 * @param ctrl Flag indicating a use of MBA controller in MBps
 *
 * @return Number of classes set
 * @retval -1 on error
 */
int IntelRDT::set_mba_clos(const unsigned clos, const uint64_t mb,
                           const unsigned socket, int ctrl)
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    struct pqos_mba mba, actual;

    if (p_sockets == NULL || mb == 0) {
        throw_with_trace(
            std::runtime_error("Failed to set MBA configuration!"));
        return -1;
    }
    mba.ctrl = ctrl;
    mba.class_id = clos;
    mba.mb_max = mb;

    int ret = pqos_mba_set(socket, 1, &mba, &actual);

    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Method pqos_mba_set FAILED!"));

    LOGINF("SOCKET {} MBA CLOS {} => {} MBps"_format(socket, actual.class_id,
                                                     actual.mb_max));

    return 1;
}

/*
 * @brief Sets memory bandwidth to a given CLOS
 *
 * @param [in] clos CLOS id that will be limit the mb
 * @param [in] socket CPU socket id
 * @param [in] ctrl Flag indicating a use of MBA controller in MBps
 * @param [in] mb amount of memory bandwidth to be set
 */
void IntelRDT::set_mb(uint32_t clos, uint32_t socket, int ctrl, unsigned mb)
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    struct pqos_mba requested, actual;

    if (p_sockets == NULL || mb == 0)
        throw_with_trace(
            std::runtime_error("FAILED to set MBA configuration!"));

    /*Set CLOS new MBA configuration*/
    requested.ctrl = ctrl;
    requested.class_id = clos;
    requested.mb_max = mb;

    int ret = pqos_mba_set(socket, 1, &requested, &actual);

    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Method pqos_mba_set FAILED!"));

    LOGINF("SOCKET {} MBA CLOS {} => {} MBps"_format(socket, actual.class_id,
                                                     actual.mb_max));
}

/*
 * @brief Gets memory bandwidth to a given CLOS
 *
 * @param [in] clos CLOS id that will be limit the mb
 * @param [in] socket CPU socket id
 *
 * @return memory bandwidth assigned to the given CLOS
 */

uint64_t IntelRDT::get_mb(uint32_t clos, uint32_t socket)
{
    if (!initialized)
        throw_with_trace(std::runtime_error(
            "Could not reset: init method must be called first"));

    const struct pqos_capability *cap = NULL;
    const struct pqos_cap_mba *mba_cap = NULL;
    int ret = PQOS_RETVAL_OK;

    ret = pqos_cap_get_type(p_cap, PQOS_CAP_TYPE_MBA, &cap);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(
            std::runtime_error("Method pqos_cap_get_type FAILED!!"));

    mba_cap = cap->u.mba;

    struct pqos_mba tab[mba_cap->num_classes];
    unsigned num = 0;

    ret = pqos_mba_get(socket, mba_cap->num_classes, &num, tab);

    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Method pqos_mba_get FAILED!!"));
    else
        LOGINF("SOCKET {} MBA CLOS {} => {} MBps"_format(
            socket, tab[clos].class_id, tab[clos].mb_max));

    return tab[clos].mb_max;
}

/**
 * @brief Starts monitoring LLC occupancy and memory BWs. of a given pid
 *
 * @param [in] pid: PID of process to be monitored
 *
 * @return Operation status
 * @retval 0 OK
 * @retval -1 error
 */
int IntelRDT::monitor_setup_pid(pid_t pid)
{
    unsigned i;
    int ret;
    const struct pqos_capability *cap_mon = NULL;
    void *context = NULL;
    enum pqos_mon_event all_evts = (enum pqos_mon_event)0;

    // Get monitoring capabilities:
    ret = pqos_cap_get_type(p_cap, PQOS_CAP_TYPE_MON, &cap_mon);
    if (ret == PQOS_RETVAL_PARAM)
        printf("Error retrieving monitoring capabilities!\n");

    // Get all available events on this platform
    for (i = 0; i < cap_mon->u.mon->num_events; i++) {
        struct pqos_monitor *mon = &cap_mon->u.mon->events[i];
        LOGINF("EVENT SUPPORTED: {}"_format(mon->type));
        // Include only LLC occup and MBM events
        if (mon->type <= 8) {
            all_evts = static_cast<pqos_mon_event>(
                static_cast<int>(mon->type | all_evts));
            LOGINF("--> EVENT INCLUDED: {}"_format(mon->type));
        }
    }

    unsigned index = num_pids;
    if (free_pids) {
        index = free_pids_index;
        LOGINF("Free_pids true! index {}"_format(index));
    }
    m_mon_grps[index] = (pqos_mon_data *)malloc(sizeof(**m_mon_grps));
    sel_monitor_pid_tab[index].pgrp = m_mon_grps[index];
    sel_monitor_pid_tab[index].pid = pid;

    LOGINF("os_mon_start_pids...");
    ret = pqos_mon_start_pids(1, &pid, all_evts, context,
                              sel_monitor_pid_tab[index].pgrp);
    LOGINF("os_mon_start_pids DONE");

    //Any problem with monitoring the process?
    if (ret != PQOS_RETVAL_OK) {
        LOGINF("PID {} monitoring start error, status {}"_format(pid, ret));
        throw_with_trace(
            std::runtime_error("Method os_mon_start_pids FAILED!!"));
        return -1;
    }

    if (!free_pids) {
        num_pids++;
        LOGINF("Free_pids {} so update num_pids"_format(free_pids));
    } else {
        free_pids = false;
    }

    LOGINF("num_pids: {}"_format(num_pids));

    LOGINF("PQOS EVENTS SUCCESSFULLY SETUP");
    return 0;
}

/**
 * @brief Returns value of a given counter for a given pid
 *
 * @param [in] pid: PID of process to be monitored
 * @param [in] event: value of counter to return
 *
 * @return counter reading
 */
void IntelRDT::monitor_get_values_pid(pid_t pid, double *llc_occup,
                                      double *lmem_bw, double *tmem_bw,
                                      double *rmem_bw)
{
    unsigned i, i_pid;
    int ret;
    double val = 0;

    // Poll all groups
    ret = os_mon_poll(m_mon_grps, (unsigned)num_pids);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Method os_mon_poll FAILED!!"));

    // Get group for wanted PID
    for (i = 0; i < num_pids; i++) {
        if (m_mon_grps[i]->pids[0] == pid) {
            i_pid = i;
            break;
        }
    }

    // Get value of the event passed as parameter
    struct pqos_event_values *pv = &m_mon_grps[i_pid]->values;

    *llc_occup = pv->llc / (1024.0 * 1024.0);
    *lmem_bw = pv->mbm_local / (1024.0 * 1024.0);
    *tmem_bw = pv->mbm_total / (1024.0 * 1024.0);

    if (pv->mbm_total > pv->mbm_local)
        *rmem_bw = (pv->mbm_total - pv->mbm_local) / (1024.0 * 1024.0);
    else
        *rmem_bw = 0;
}

/**
 * @brief Stops monitoring LLC occupancy and memory BWs. of a given pid
 *
 * @param [in] pid: PID of process to stop monitoring
 *
 * @return Operation status
 * @retval 0 OK
 * @retval -1 error
 */
int IntelRDT::monitor_stop_pid(pid_t pid)
{
    // Get group for wanted PID
    for (uint32_t i = 0; i < num_pids; i++) {
        if (m_mon_grps[i]->pids[0] == pid) {
            free_pids_index = i;
            free_pids = true;

            int ret = pqos_mon_stop(m_mon_grps[i]);

            if (ret != PQOS_RETVAL_OK) {
                throw_with_trace(std::runtime_error("Monitoring stop error!"));
                return -1;
            }

            free(m_mon_grps[i]);
            LOGINF("Stop PQOS monitoring for task {}"_format(pid));
            break;
        }
    }
    return 0;
}

/**
 * @brief Starts monitoring LLC occupancy and memory BWs. of a given core
 *
 * @param [in] core: core number to be monitored
 *
 * @return Operation status
 * @retval 0 OK
 * @retval -1 error
 */
int IntelRDT::monitor_setup_core(uint32_t core)
{
    unsigned i;
    int ret;
    const struct pqos_capability *cap_mon = NULL;
    void *context = NULL;
    enum pqos_mon_event all_evts = (enum pqos_mon_event)0;

    // Get monitoring capabilities:
    ret = pqos_cap_get_type(p_cap, PQOS_CAP_TYPE_MON, &cap_mon);
    if (ret == PQOS_RETVAL_PARAM)
        printf("Error retrieving monitoring capabilities!\n");

    // Get all available events on this platform
    for (i = 0; i < cap_mon->u.mon->num_events; i++) {
        struct pqos_monitor *mon = &cap_mon->u.mon->events[i];
        LOGINF("EVENT SUPPORTED: {}"_format(mon->type));
        // Include only LLC occup and MBM events
        if (mon->type <= 8) {
            all_evts = static_cast<pqos_mon_event>(
                static_cast<int>(mon->type | all_evts));
            LOGINF("--> EVENT INCLUDED: {}"_format(mon->type));
        }
    }

    unsigned index = num_cores;
    if (free_cores) {
        index = free_cores_index;
        LOGINF("Free_cores true! index {}"_format(index));
    }

    m_mon_grps[index] = (pqos_mon_data *)malloc(sizeof(**m_mon_grps));
    sel_monitor_core_tab[index].pgrp = m_mon_grps[index];
    sel_monitor_core_tab[index].core = core;

    LOGINF("os_mon_start...");
    ret = pqos_mon_start(1, &core, all_evts, context,
                         sel_monitor_core_tab[index].pgrp);
    LOGINF("os_mon_start DONE");

    //Any problem with monitoring the process?
    if (ret != PQOS_RETVAL_OK) {
        LOGINF("Core {} monitoring start error, status {}"_format(core, ret));
        throw_with_trace(std::runtime_error("Method os_mon_start FAILED!!"));
        return -1;
    }

    if (!free_cores) {
        num_cores++;
        LOGINF("Free_cores {} so update num_pids"_format(free_cores));
    } else {
        free_cores = false;
    }

    LOGINF("num_cores: {}"_format(num_cores));

    LOGINF("PQOS EVENTS SUCCESSFULLY SETUP");
    return 0;
}

/**
 * @brief Returns value of a given counter for a given core
 *
 * @param [in] core: core number to be monitored
 * @param [in] event: value of counter to return
 *
 * @return counter reading
 */
void IntelRDT::monitor_get_values_core(uint32_t core, double *llc_occup,
                                       double *lmem_bw, double *tmem_bw,
                                       double *rmem_bw)
{
    unsigned i, i_core;
    int ret;
    double val = 0;

    // Poll all groups
    ret = os_mon_poll(m_mon_grps, (unsigned)num_cores);
    if (ret != PQOS_RETVAL_OK)
        throw_with_trace(std::runtime_error("Method os_mon_poll FAILED!!"));

    // Get group for wanted core
    for (i = 0; i < num_cores; i++) {
        if (m_mon_grps[i]->cores[0] == core) {
            i_core = i;
            break;
        }
    }

    // Get value of the event passed as parameter
    struct pqos_event_values *pv = &m_mon_grps[i_core]->values;

    *llc_occup = pv->llc / (1024.0 * 1024.0);
    *lmem_bw = pv->mbm_local / (1024.0 * 1024.0);
    *tmem_bw = pv->mbm_total / (1024.0 * 1024.0);

    if (pv->mbm_total > pv->mbm_local)
        *rmem_bw = (pv->mbm_total - pv->mbm_local) / (1024.0 * 1024.0);
    else
        *rmem_bw = 0;
}

/**
 * @brief Stops monitoring LLC occupancy and memory BWs. of a given core
 *
 * @param [in] core: comre number to stop monitoring
 *
 * @return Operation status
 * @retval 0 OK
 * @retval -1 error
 */
int IntelRDT::monitor_stop_core(uint32_t core)
{
    // Get group for wanted core
    for (uint32_t i = 0; i < num_cores; i++) {
        if (m_mon_grps[i]->cores[0] == core) {
            free_cores_index = i;
            free_cores = true;

            int ret = pqos_mon_stop(m_mon_grps[i]);

            if (ret != PQOS_RETVAL_OK) {
                throw_with_trace(std::runtime_error("Monitoring stop error!"));
                return -1;
            }

            free(m_mon_grps[i]);
            LOGINF("Stop PQOS monitoring for core {}"_format(core));
            break;
        }
    }

    return 0;
}
