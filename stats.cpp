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

#include <functional>
#include <iomanip>
#include <iosfwd>
#include <iostream>
#include <sstream>

#include <boost/io/ios_state.hpp>
#include <fmt/format.h>

#include "log.hpp"
#include "stats.hpp"
#include "throw-with-trace.hpp"

#define WIN_SIZE 7

namespace acc = boost::accumulators;

using fmt::literals::operator""_format;

Stats::Stats(const std::vector<std::string> &stats_names,
             const double interval_ti)
{
    init(stats_names, interval_ti);
}

void Stats::init_derived_metrics_total(
    const std::vector<std::string> &stats_names, const double interval_ti)
{
    bool instructions = std::find(stats_names.begin(), stats_names.end(),
                                  "inst_retired.any") != stats_names.end();
    bool cycles = std::find(stats_names.begin(), stats_names.end(), "cycles") !=
                  stats_names.end();
    bool ref_cycles =
        std::find(stats_names.begin(), stats_names.end(),
                  "cpu_clk_unhalted.ref_tsc") != stats_names.end();
    bool misses_l2 = std::find(stats_names.begin(), stats_names.end(),
                               "mem_load_retired.l2_miss") != stats_names.end();

    bool misses_l3 = std::find(stats_names.begin(), stats_names.end(),
                               "mem_load_retired.l3_miss") != stats_names.end();

    bool read_disk = std::find(stats_names.begin(), stats_names.end(),
                               "Read_bytes_sec") != stats_names.end();
    bool write_disk = std::find(stats_names.begin(), stats_names.end(),
                                "Write_bytes_sec") != stats_names.end();
    bool time_disk = std::find(stats_names.begin(), stats_names.end(),
                               "Time_io_disk_ns") != stats_names.end();

    if (time_disk) {
        derived_metrics_total.push_back(std::make_pair("iostat", [this]() {
            double t_cycle =
                0.000000000476190476190476; // Assuming 2.1 GHz frequency
            double t_disk = this->sum("Time_io_disk_ns");
            return (t_disk / 10000000000) / t_cycle;
        }));
    }

    if (read_disk && write_disk) {
        derived_metrics_total.push_back(
            std::make_pair("Disk_BW[MBps]", [this]() {
                double read = this->sum("Read_bytes_sec");
                double write = this->sum("Write_bytes_sec");
                return 0;
            }));
    }

    if (instructions && cycles) {
        derived_metrics_total.push_back(std::make_pair("ipc", [this]() {
            double inst = this->sum("inst_retired.any");
            double cycl = this->sum("cycles");
            return inst / cycl;
        }));
    }

    if (instructions && ref_cycles) {
        derived_metrics_total.push_back(std::make_pair("ref-ipc", [this]() {
            double inst = this->sum("inst_retired.any");
            double ref_cycl = this->sum("cpu_clk_unhalted.ref_tsc");
            return inst / ref_cycl;
        }));
    }

    if (instructions && misses_l2) {
        derived_metrics_total.push_back(std::make_pair("mpki-l2", [this]() {
            double inst = this->sum("inst_retired.any");
            double ml2 = this->sum("mem_load_retired.l2_miss");
            return (1000 * ml2) / inst;
        }));
    }

    if (instructions && misses_l3) {
        derived_metrics_total.push_back(std::make_pair("mpki-l3", [this]() {
            double inst = this->sum("inst_retired.any");
            double ml3 = this->sum("mem_load_retired.l3_miss");
            return (1000 * ml3) / inst;
        }));
    }
}

void Stats::init_derived_metrics_int(
    const std::vector<std::string> &stats_names, const double interval_ti)
{
    bool instructions = std::find(stats_names.begin(), stats_names.end(),
                                  "inst_retired.any") != stats_names.end();
    bool cycles = std::find(stats_names.begin(), stats_names.end(), "cycles") !=
                  stats_names.end();
    bool ref_cycles =
        std::find(stats_names.begin(), stats_names.end(),
                  "cpu_clk_unhalted.ref_tsc") != stats_names.end();

    bool misses_l2 = std::find(stats_names.begin(), stats_names.end(),
                               "mem_load_retired.l2_miss") != stats_names.end();

    bool misses_l3 = std::find(stats_names.begin(), stats_names.end(),
                               "mem_load_retired.l3_miss") != stats_names.end();

    bool read_disk = std::find(stats_names.begin(), stats_names.end(),
                               "Read_bytes_sec") != stats_names.end();
    bool write_disk = std::find(stats_names.begin(), stats_names.end(),
                                "Write_bytes_sec") != stats_names.end();
    bool time_disk = std::find(stats_names.begin(), stats_names.end(),
                               "Time_io_disk_ns") != stats_names.end();

    if (time_disk) {
        derived_metrics_int.push_back(std::make_pair("iostat", [this]() {
            double t_cycle =
                0.000000000476190476190476; // Assuming 2.1 GHz frequency
            double t_disk = this->last("Time_io_disk_ns");
            return (t_disk / 10000000000) / t_cycle;
        }));
    }

    if (read_disk && write_disk) {
        derived_metrics_int.push_back(
            std::make_pair("Disk_BW[MBps]", [this, interval_ti]() {
                double read = this->last("Read_bytes_sec");
                double write = this->last("Write_bytes_sec");
                return ((read + write) / (double)interval_ti) / 1024 / 1024;
            }));
    }

    if (instructions && cycles) {
        derived_metrics_int.push_back(std::make_pair("ipc", [this]() {
            double inst = this->last("inst_retired.any");
            double cycl = this->last("cycles");
            return inst / cycl;
        }));
    }

    if (instructions && ref_cycles) {
        derived_metrics_int.push_back(std::make_pair("ref-ipc", [this]() {
            double inst = this->last("inst_retired.any");
            double ref_cycl = this->last("cpu_clk_unhalted.ref_tsc");
            return inst / ref_cycl;
        }));
    }

    if (instructions && misses_l2) {
        derived_metrics_int.push_back(std::make_pair("mpki-l2", [this]() {
            double inst = this->last("inst_retired.any");
            double ml2 = this->last("mem_load_retired.l2_miss");
            return (1000 * ml2) / inst;
        }));
    }

    if (instructions && misses_l3) {
        derived_metrics_int.push_back(std::make_pair("mpki-l3", [this]() {
            double inst = this->last("inst_retired.any");
            double ml3 = this->last("mem_load_retired.l3_miss");
            return (1000 * ml3) / inst;
        }));
    }
}

void Stats::init(const std::vector<std::string> &stats_names,
                 const double interval_ti)
{
    assert(!initialized);

    for (const auto &c : stats_names)
        events.insert(std::make_pair(
            c, accum_t(acc::tag::rolling_window::window_size = WIN_SIZE)));

    init_derived_metrics_int(stats_names, interval_ti);
    init_derived_metrics_total(stats_names, interval_ti);

    // Test der metrics
    if (derived_metrics_int.size() != derived_metrics_total.size())
        throw_with_trace(std::runtime_error(
            "Different number of derived metrics for int ({}) and total ({})"_format(
                derived_metrics_int.size(), derived_metrics_total.size())));

    for (const auto &der : derived_metrics_int) {
        auto it = std::find_if(
            derived_metrics_total.begin(), derived_metrics_total.end(),
            [&der](const auto &tuple) { return tuple.first == der.first; });
        if (it == derived_metrics_total.end())
            throw_with_trace(std::runtime_error(
                "Different derived metrics for int and total results"));
    }

    for (const auto &der : derived_metrics_int)
        events.insert(std::make_pair(
            der.first,
            accum_t(acc::tag::rolling_window::window_size = WIN_SIZE)));

    // Store the names of the counters
    names = stats_names;

    initialized = true;
}

Stats &Stats::accum(const counters_t &counters, const double interval_ti)
{
    assert(initialized);

    clast = ccurr;
    ccurr = counters;

    const auto &last_id_idx = clast.get<by_id>();
    auto &curr_id_idx = ccurr.get<by_id>();

    assert(!ccurr.empty());

    // App has just started, no last data
    if (clast.empty()) {
        auto it = curr_id_idx.cbegin();
        while (it != curr_id_idx.cend()) {
            double value = (it->name == "power/energy-ram/" ||
                            it->name == "power/energy-pkg/")
                               ? 0
                               : it->value;

            assert(it->running >= 0 && it->running <= it->enabled);

            if (it->running)
                value /= (double)it->running / (double)it->enabled;

            if ((value < 0) || (value != value) || std::isfinite(value))
                value = 0;

            assert(std::isfinite(value));
            events.at(it->name)(value);
            it++;
        }
        cbak = counters; // For being able to iterate them later
    }

    // We have data from the last interval
    else {
        assert(ccurr.size() == clast.size());
        auto curr_it = curr_id_idx.cbegin();
        auto last_it = last_id_idx.cbegin();
        while (curr_it != curr_id_idx.cend() && last_it != last_id_idx.cend()) {
            const Counter &c = *curr_it;
            const Counter &l = *last_it;
            assert(c.id == l.id);
            assert(c.name == l.name);

            double value = c.snapshot ? c.value : c.value - l.value;

            // Print difference values for memory BW in MB/s
            // since this is the real amount
            if (c.name == "MBL[MBps]" || c.name == "MBR[MBps]" ||
                c.name == "MBT[MBps]") {
                value = value / (double)interval_ti;
                //LOGINF("{}: {} MB/s"_format(c.name, value));
            }

            if (value < 0) {
                // There has been an overflow with the energy, and we have to correct it
                double newvalue = 0;
                if (c.name == "MBL[MBps]" || c.name == "MBR[MBps]" ||
                    c.name == "MBT[MBps]" || c.name == "Rx_netBW[KBps]" ||
                    c.name == "Tx_netBW[KBps]" ||
                    c.name == "OVS_Rx_netBW[KBps]" ||
                    c.name == "OVS_Tx_netBW[KBps]" ||
                    c.name == "Time_io_disk_ns")
                    newvalue = 0;
                else if (c.name == "power/energy-pkg/") {
                    newvalue = c.value * 1E6 +
                               (read_max_ujoules_pkg() - l.value * 1E6);
                    newvalue /= 1E6;
                } else if (c.name == "power/energy-ram/") {
                    newvalue = c.value * 1E6 +
                               (read_max_ujoules_ram() - l.value * 1E6);
                    newvalue /= 1E6;
                } else
                    throw_with_trace(std::runtime_error(
                        "Negative interval value ({}) for the counter '{}'"_format(
                            value, c.name)));

                LOGDEB(
                    "Energy counter '{}' overflow. Last interval value was {}. Current will be {}"_format(
                        c.name, last(c.name), newvalue));
                value = newvalue;
            }

            assert(c.enabled >= 0 && c.running <= c.enabled);

            double enabled_fraction = (double)c.running / (double)c.enabled;
            if (c.enabled == 0)
                LOGINF(
                    "Counter '{}' was not enabled during this interval"_format(
                        c.name));
            else if (enabled_fraction < 1) {
                value /= enabled_fraction;
                LOGDEB("Counter {} has been scaled ({})"_format(
                    c.name, enabled_fraction));
            } else {
                assert(enabled_fraction == 1);
                LOGDEB(
                    "Counter {} has been read without scaling"_format(c.name));
            }

            //LOGINF("----> {}: {}"_format(c.name, value));
            //assert(std::isfinite(value) || (value == 0));
            if (!std::isfinite(value))
                value = 0;
            events.at(c.name)(value);

            // Perf reports events since the begining of the execution, but enabled and running times are for the interval.
            // Therefore, in order to know the running and enabled times since the start we need to accumulate them.
            curr_id_idx.modify(curr_it, [&l](auto &c_) {
                c_.enabled += l.enabled;
                c_.running += l.running;
            });

            // Check values of each counter
            //LOGDEB("Counter {} has value {}"_format(c.name, c.value));
            curr_it++;
            last_it++;
        }
    }

    // Compute and add derived metrics
    for (const auto &der : derived_metrics_int)
        events.at(der.first)(der.second());

    counter++;

    return *this;
}

std::string Stats::header_to_string(const std::string &sep) const
{
    if (!names.size())
        return "";

    std::stringstream ss;
    auto it = names.begin();
    ss << *it;
    it++;
    for (; it != names.end(); it++)
        ss << sep << *it;
    for (
        const auto &der :
        derived_metrics_int) // Int, snapshot and total have the same derived metrics
        ss << sep << der.first;
    return ss.str();
}

// Some of the counters are collected as snapshots of the state of the system (i.e. the cache space occupation).
// This is taken into account to compute the value of the metric for the interval. If force_snapshot is true, then
// the function prints the value of the counter. If not, it prints the difference with the previous interval, unless
// the metric is collected as an snapshot.
std::string Stats::data_to_string_total(const std::string &sep) const
{
    std::stringstream ss;

    assert(cbak.size() > 0);

    const auto &cbak_id_idx = cbak.get<by_id>();
    auto it = cbak_id_idx.cbegin();
    while (it != cbak_id_idx.cend()) {
        const auto &name = it->name;
        const accum_t &event = events.at(name);
        double value = it->snapshot ? acc::mean(event) : acc::sum(event);

        if (name == "MBL[MBps]" || name == "MBR[MBps]" || name == "MBT[MBps]")
            value = acc::mean(event);

        ss << value;
        it++;
        if (it != cbak_id_idx.cend())
            ss << sep;
    }

    // Derived metrics
    for (auto it1 = derived_metrics_total.cbegin();
         it1 != derived_metrics_total.cend(); it1++) {
        double value = it1->second();
        ss << sep << value;
    }

    return ss.str();
}

std::string Stats::double2hexstr(double x) const
{
    int int_x = (int)x;
    char buf[255];
    snprintf(buf, sizeof(buf), "%02x", int_x);

    return "0x" + std::string(buf);
}

std::string Stats::data_to_string_int(const std::string &sep) const
{
    std::stringstream ss;

    assert(names.size() > 0);

    auto it1 = names.cbegin();
    while (it1 != names.cend()) {
        if (*it1 == "clos_mask") {
            double val_clos = (double)acc::last(events.at(*it1));
            std::string s_clos = double2hexstr(val_clos);
            ss << s_clos;
        } else if (*it1 == "Time[ns]") {
            ss << acc::last(events.at(*it1));
        } else
            ss << acc::last(events.at(*it1));

        it1++;

        if (it1 != names.cend())
            ss << sep;
    }

    // Derived metrics
    for (auto it = derived_metrics_int.cbegin();
         it != derived_metrics_int.cend(); it++) {
        double value = it->second();
        ss << sep << value;
    }

    return ss.str();
}

double Stats::get_current(const std::string &name) const
{
    const auto &curr_index = ccurr.get<by_name>();
    auto it = curr_index.find(name);
    if (it == curr_index.end())
        throw_with_trace(
            std::runtime_error("Event not monitorized '{}'"_format(name)));
    if (it->value == 0)
        return 0; // This way we don't have to worry about enabled being 0
    return it->value / ((double)it->running / (double)it->enabled);
}

double Stats::sum(const std::string &name) const
{
    return acc::sum(events.at(name));
}

double Stats::last(const std::string &name) const
{
    return acc::last(events.at(name));
}

void Stats::reset_counters()
{
    clast = counters_t();
    ccurr = counters_t();
}
