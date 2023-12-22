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
#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// CPU USAGE
const int NUM_CPU_STATES = 10;

enum CPUStates {
    S_USER = 0,
    S_NICE,
    S_SYSTEM,
    S_IDLE,
    S_IOWAIT,
    S_IRQ,
    S_SOFTIRQ,
    S_STEAL,
    S_GUEST,
    S_GUEST_NICE
};

typedef struct CPUData {
    std::string cpu;
    size_t times[NUM_CPU_STATES];
} CPUData;

std::ifstream open_ifstream(const boost::filesystem::path &path);
std::ofstream open_ofstream(const boost::filesystem::path &path);

std::string extract_executable_name(const std::string &cmd);
void dir_copy(const std::string &source, const std::string &dest);
void dir_copy_contents(const std::string &source, const std::string &dest);
std::string random_string(size_t length);
void drop_privileges();
int get_self_cpu_id();
void set_cpu_affinity(std::vector<uint32_t> cpus, pid_t pid = 0);
void assert_dir_exists(const boost::filesystem::path &dir);
void pid_get_children_rec(const pid_t pid, std::vector<pid_t> &children);

// Get logical CPU utilzation
double getTemperatureCPU(uint32_t core);
void ReadStatsCPU(std::vector<CPUData> &entries);
size_t GetIdleTime(const CPUData &e);
size_t GetTime(const CPUData & e, std::string time);
size_t GetIowaitTime(const CPUData &e);
float get_cpu_utilization(const std::vector<CPUData> &entries1,
                          const std::vector<CPUData> &entries2, uint64_t cpu);
float get_time_utilization(const std::vector<CPUData> & entries1, const std::vector<CPUData> & entries2, uint64_t cpu, std::string time);

// Measure the time the passed callable object consumes
template <typename TimeT = std::chrono::milliseconds> struct measure {
    template <typename F, typename... Args>
    static typename TimeT::rep execution(F func, Args &&... args)
    {
        namespace chr = std::chrono;
        auto start = chr::system_clock::now();

        // Now call the function with all the parameters you need.
        func(std::forward<Args>(args)...);

        auto duration =
            chr::duration_cast<TimeT>(chr::system_clock::now() - start);

        return duration.count();
    }
};
