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

#include <fmt/format.h>

extern "C" {
#include <libminiperf.h>
}

#include "common.hpp"
#include "events-perf.hpp"
#include "log.hpp"
#include "throw-with-trace.hpp"

using fmt::literals::operator""_format;

double read_energy_pkg();
double read_energy_ram();

void Perf::set_perf_type(const std::string type)
{
    perf_type = type;
}

std::string Perf::get_perf_type()
{
    return perf_type;
}

void Perf::init()
{
}

void Perf::clean()
{
    for (const auto &item : id_events)
        for (const auto &evlist : item.second.groups)
            ::clean(evlist);
}

void Perf::clean(int32_t id)
{
    for (const auto &evlist : id_events.at(id).groups)
        ::clean(evlist);
    id_events.erase(id);
}

void Perf::setup_events(int32_t id, const std::vector<std::string> &groups)
{
    //assert(pid >= 1);
    for (const auto &events : groups) {
        LOGINF("Events: {}"_format(events));
        const auto evlist = ::setup_events(std::to_string(id).c_str(),
                                           events.c_str(), perf_type.c_str());

        if (evlist == NULL)
            throw_with_trace(std::runtime_error(
                "Could not setup events '{}'"_format(events)));
        if (::num_entries(evlist) >= max_num_events)
            throw_with_trace(std::runtime_error("Too many events"));
        id_events[id].append(evlist);
        ::enable_counters(evlist);
    }
}

void Perf::enable_counters(int32_t id)
{
    for (const auto &evlist : id_events[id].groups)
        ::enable_counters(evlist);
}

uint64_t read_max_ujoules_ram()
{
    // TODO: This needs improvement... i.e. consider more packages etc.
    auto fdata = open_ifstream(
        "/sys/class/powercap/intel-rapl:0/intel-rapl:0:0/max_energy_range_uj");
    auto fname =
        open_ifstream("/sys/class/powercap/intel-rapl:0/intel-rapl:0:0/name");
    uint64_t data;

    fdata >> data;

    std::string name;
    fname >> name;

    assert(name == "dram");

    return data;
}

uint64_t read_max_ujoules_pkg()
{
    // TODO: This needs improvement... i.e. consider more packages etc.
    auto fdata =
        open_ifstream("/sys/class/powercap/intel-rapl:0/max_energy_range_uj");
    auto fname = open_ifstream("/sys/class/powercap/intel-rapl:0/name");
    uint64_t data;

    fdata >> data;

    std::string name;
    fname >> name;

    assert(name == "package-0");

    return data;
}

double read_energy_ram()
{
    // TODO: This needs improvement... i.e. consider more packages etc.
    auto fdata = open_ifstream(
        "/sys/class/powercap/intel-rapl:0/intel-rapl:0:0/energy_uj");
    auto fname =
        open_ifstream("/sys/class/powercap/intel-rapl:0/intel-rapl:0:0/name");
    uint64_t data;

    fdata >> data;

    std::string name;
    fname >> name;

    LOGDEB("RAM energy: " << data);

    assert(name == "dram");

    return (double)data / 1E6; // Convert it to joules
}

double read_energy_pkg()
{
    // TODO: This needs improvement... i.e. consider more packages etc.
    auto fdata = open_ifstream("/sys/class/powercap/intel-rapl:0/energy_uj");
    auto fname = open_ifstream("/sys/class/powercap/intel-rapl:0/name");
    uint64_t data;

    fdata >> data;

    std::string name;
    fname >> name;

    LOGDEB("PKG energy: " << data);

    assert(name == "package-0");

    return (double)data / 1E6; // Convert it to joules
}

std::vector<counters_t>
Perf::read_counters(pid_t pid, int32_t id, double llc_occup_value,
                    double lmem_bw_value, double tmem_bw_value,
                    double rmem_bw_value, DiskUtils DU,
                    float network_bwtx, float network_bwrx, double ovs_bwtx,
                    double ovs_bwrx, uint64_t time_interval)
{
    const char *names[max_num_events];
    double results[max_num_events];
    const char *units[max_num_events];
    bool snapshot[max_num_events];
    uint64_t enabled[max_num_events];
    uint64_t running[max_num_events];

    const auto epkg = "power/energy-pkg/";
    const auto eram = "power/energy-ram/";

    // Entries for events monitored by Intel
    const auto llc_occup = "LLC_occup[MB]";
    const auto lmem_bw = "MBL[MBps]";
    const auto tmem_bw = "MBT[MBps]";
    const auto rmem_bw = "MBR[MBps]";

    // Entries for monitored disk events
    const auto read_bytes = "Read_bytes_sec";
    const auto write_bytes = "Write_bytes_sec";
    const auto read_iops = "Read_iops_sec";
    const auto write_iops = "Write_iops_sec";
	const auto time_io_disk = "Time_io_disk_ns";

    // Entries for monitored network events
    const auto rx_bw = "Rx_netBW[KBps]";
    const auto tx_bw = "Tx_netBW[KBps]";

    // Entries for monitored network events
    const auto ovs_rx_bw = "OVS_Rx_netBW[KBps]";
    const auto ovs_tx_bw = "OVS_Tx_netBW[KBps]";

    // Entries for time
    const auto time_int = "Time[ns]";

    auto result = std::vector<counters_t>();

    bool first = true;

    for (const auto &evlist : id_events[id].groups) {
        int n = ::num_entries(evlist);
        auto counters = counters_t();
        ::read_counters(evlist, names, results, units, snapshot, enabled,
                        running);
        int i;

        //LOGINF("------ {} ------"_format(id));
        for (i = 0; i < n; i++) {
            assert(running[i] <= enabled[i]);
            counters.insert({i, names[i], results[i], units[i], snapshot[i],
                             enabled[i], running[i]});
            //LOGINF("{} {}"_format(names[i], results[i]));
        }
        //LOGINF("------------------");
        // Put energy and Intel measurements only in the first group
        if (first) {
            counters.insert({i++, epkg, read_energy_pkg(), "j", false, 1, 1});
            counters.insert({i++, eram, read_energy_ram(), "j", false, 1, 1});

            counters.insert({i++, llc_occup, llc_occup_value, "", true, 1, 1});
            counters.insert({i++, lmem_bw, lmem_bw_value, "", false, 1, 1});
            counters.insert({i++, tmem_bw, tmem_bw_value, "", false, 1, 1});
            counters.insert({i++, rmem_bw, rmem_bw_value, "", false, 1, 1});

            //LOGINF("Read_bytes: {}"_format(DU.get_read_bytes_sec()));
            counters.insert(
                {i++, read_bytes, DU.get_read_bytes_sec(), "", false, 1, 1});
            counters.insert(
                {i++, write_bytes, DU.get_write_bytes_sec(), "", false, 1, 1});
            counters.insert(
                {i++, read_iops, DU.get_read_iops_sec(), "", false, 1, 1});
            counters.insert(
                {i++, write_iops, DU.get_write_iops_sec(), "", false, 1, 1});
			counters.insert(
                {i++, time_io_disk, DU.get_disk_io_time(), "", false, 1, 1});


            counters.insert({i++, tx_bw, network_bwtx, "", true, 1, 1});
            counters.insert({i++, rx_bw, network_bwrx, "", true, 1, 1});

            counters.insert({i++, ovs_tx_bw, ovs_bwtx, "", true, 1, 1});
            counters.insert({i++, ovs_rx_bw, ovs_bwrx, "", true, 1, 1});

            counters.insert({i++, time_int, time_interval, "", true, 1, 1});

            first = false;
        }
        result.push_back(counters);
    }
    return result;
}

std::vector<counters_t>
Perf::read_counters(pid_t pid, int32_t id, double llc_occup_value,
                    double lmem_bw_value, double tmem_bw_value,
                    double rmem_bw_value, uint64_t time_interval)
{
    const char *names[max_num_events];
    double results[max_num_events];
    const char *units[max_num_events];
    bool snapshot[max_num_events];
    uint64_t enabled[max_num_events];
    uint64_t running[max_num_events];

    const auto epkg = "power/energy-pkg/";
    const auto eram = "power/energy-ram/";

    // Entries for events monitored by Intel
    const auto llc_occup = "LLC_occup[MB]";
    const auto lmem_bw = "MBL[MBps]";
    const auto tmem_bw = "MBT[MBps]";
    const auto rmem_bw = "MBR[MBps]";

    // Entries for time
    const auto time_int = "Time[ns]";

    auto result = std::vector<counters_t>();

    bool first = true;

    for (const auto &evlist : id_events[id].groups) {
        int n = ::num_entries(evlist);
        auto counters = counters_t();
        ::read_counters(evlist, names, results, units, snapshot, enabled,
                        running);
        int i;

        //LOGINF("------ {} ------"_format(id));
        for (i = 0; i < n; i++) {
            assert(running[i] <= enabled[i]);
            counters.insert({i, names[i], results[i], units[i], snapshot[i],
                             enabled[i], running[i]});
            //LOGINF("{} {}"_format(names[i], results[i]));
        }
        //LOGINF("------------------");
        // Put energy and Intel measurements only in the first group
        if (first) {
            counters.insert({i++, epkg, read_energy_pkg(), "j", false, 1, 1});
            counters.insert({i++, eram, read_energy_ram(), "j", false, 1, 1});

            counters.insert({i++, llc_occup, llc_occup_value, "", true, 1, 1});
            counters.insert({i++, lmem_bw, lmem_bw_value, "", false, 1, 1});
            counters.insert({i++, tmem_bw, tmem_bw_value, "", false, 1, 1});
            counters.insert({i++, rmem_bw, rmem_bw_value, "", false, 1, 1});

            counters.insert({i++, time_int, time_interval, "", true, 1, 1});

            first = false;
        }
        result.push_back(counters);
    }
    return result;
}

std::vector<std::vector<std::string>> Perf::get_names(int32_t id,
                                                      std::string type)
{
    const char *names[max_num_events];
    auto r = std::vector<std::vector<std::string>>();

    const auto epkg = "power/energy-pkg/";
    const auto eram = "power/energy-ram/";

    // Entries for events monitored by Intel
    const auto llc_occup = "LLC_occup[MB]";
    const auto lmem_bw = "MBL[MBps]";
    const auto tmem_bw = "MBT[MBps]";
    const auto rmem_bw = "MBR[MBps]";

    // Entries for monitored disk events
    const auto read_bytes = "Read_bytes_sec";
    const auto write_bytes = "Write_bytes_sec";
    const auto read_iops = "Read_iops_sec";
    const auto write_iops = "Write_iops_sec";
	const auto time_io_disk = "Time_io_disk_ns";

    // Entries for monitored network events
    const auto rx_bw = "Rx_netBW[KBps]";
    const auto tx_bw = "Tx_netBW[KBps]";

    // Entries for monitored network events
    const auto ovs_rx_bw = "OVS_Rx_netBW[KBps]";
    const auto ovs_tx_bw = "OVS_Tx_netBW[KBps]";

    // Entries for time
    const auto time_int = "Time[ns]";

    bool first = true;
    for (const auto &evlist : id_events[id].groups) {
        int n = ::num_entries(evlist);
        auto v = std::vector<std::string>();
        ::get_names(evlist, names);
        for (int i = 0; i < n; i++)
            v.push_back(names[i]);
        // Put energy and Intel measurements only in the first group
        if (first) {
            v.push_back(epkg);
            v.push_back(eram);
            v.push_back(llc_occup);
            v.push_back(lmem_bw);
            v.push_back(tmem_bw);
            v.push_back(rmem_bw);
            if (type == "VM") {
                v.push_back(read_bytes);
                v.push_back(write_bytes);
                v.push_back(read_iops);
                v.push_back(write_iops);
				v.push_back(time_io_disk);
                v.push_back(tx_bw);
                v.push_back(rx_bw);
                v.push_back(ovs_tx_bw);
                v.push_back(ovs_rx_bw);
            }
            v.push_back(time_int);
            first = false;
        }
        r.push_back(v);
    }
    return r;
}

void Perf::print_counters(int32_t id)
{
    for (const auto &evlist : id_events[id].groups)
        ::print_counters(evlist);
}
