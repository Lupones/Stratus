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

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <fmt/format.h>
#include <glib.h>
#include <grp.h>

#include "common.hpp"
#include "log.hpp"
#include "throw-with-trace.hpp"

namespace fs = boost::filesystem;

using fmt::literals::operator""_format;

// Opens an output stream and checks for errors
std::ofstream open_ofstream(const std::string &path)
{
    std::ofstream f;
    f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    f.open(path);
    return f;
}

// Opens an intput stream and checks for errors
std::ifstream open_ifstream(const std::string &path)
{
    std::ifstream f;
    // Throw on error
    f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    f.open(path);
    return f;
}

std::ifstream open_ifstream(const boost::filesystem::path &path)
{
    return open_ifstream(path.string());
}

std::ofstream open_ofstream(const boost::filesystem::path &path)
{
    return open_ofstream(path.string());
}

void assert_dir_exists(const boost::filesystem::path &dir)
{
    if (!fs::exists(dir))
        throw_with_trace(
            std::runtime_error("Dir {} does not exist"_format(dir.string())));
    if (!fs::is_directory(dir))
        throw_with_trace(
            std::runtime_error("{} is not a directory"_format(dir.string())));
}

// Returns the executable basename from a commandline
std::string extract_executable_name(const std::string &cmd)
{
    int argc;
    char **argv;

    if (!g_shell_parse_argv(cmd.c_str(), &argc, &argv, NULL))
        throw_with_trace(
            std::runtime_error("Could not parse commandline '" + cmd + "'"));

    std::string result = boost::filesystem::basename(argv[0]);
    g_strfreev(argv); // Free the memory allocated for argv

    return result;
}

void dir_copy(const std::string &source, const std::string &dest)
{
    namespace fs = boost::filesystem;

    if (!fs::exists(source) || !fs::is_directory(source))
        throw_with_trace(
            std::runtime_error("Source directory " + source +
                               " does not exist or is not a directory"));
    if (fs::exists(dest))
        throw_with_trace(std::runtime_error("Destination directory " + dest +
                                            " already exists"));

    // Create dest
    if (!fs::create_directories(dest))
        throw_with_trace(
            std::runtime_error("Cannot create destination directory " + dest));

    dir_copy_contents(source, dest);
}

void dir_copy_contents(const std::string &source, const std::string &dest)
{
    namespace fs = boost::filesystem;

    if (!fs::exists(source) || !fs::is_directory(source))
        throw_with_trace(
            std::runtime_error("Source directory " + source +
                               " does not exist or is not a directory"));
    if (!fs::exists(dest))
        throw_with_trace(std::runtime_error("Destination directory " + dest +
                                            " does not exist"));

    typedef fs::recursive_directory_iterator RDIter;
    for (auto it = RDIter(source), end = RDIter(); it != end; ++it) {
        const auto &path = it->path();
        auto relpath = it->path().string();
        boost::replace_first(relpath, source,
                             ""); // Convert the path to a relative path

        fs::copy(path, dest + "/" + relpath);
    }
}

std::string random_string(size_t length)
{
    auto randchar = []() -> char {
        const char charset[] = "0123456789"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
    };
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    return str;
}

// Drop sudo privileges
void drop_privileges()
{
    const char *uidstr = getenv("SUDO_UID");
    const char *gidstr = getenv("SUDO_GID");
    const char *userstr = getenv("SUDO_USER");

    if (!uidstr || !gidstr || !userstr)
        return;

    const uid_t uid = std::stol(uidstr);
    const gid_t gid = std::stol(gidstr);

    if (uid == getuid() && gid == getgid())
        return;

    if (setgid(gid) < 0)
        throw_with_trace(std::runtime_error("Cannot change gid: " +
                                            std::string(strerror(errno))));

    if (initgroups(userstr, gid) < 0)
        throw_with_trace(
            std::runtime_error("Cannot change group access list: " +
                               std::string(strerror(errno))));

    if (setuid(uid) < 0)
        throw_with_trace(std::runtime_error("Cannot change uid: " +
                                            std::string(strerror(errno))));
}

int get_self_cpu_id()
{
    /* Get the the current process' stat file from the proc filesystem */
    std::ifstream myReadFile;
    myReadFile.open("/proc/self/stat");
    char cNum[10];
    int i = 0;
    int cpu_id = -1;
    bool found = false;

    while (!myReadFile.eof() & !found) {
        if (i < 38) {
            myReadFile.getline(cNum, 256, ' ');
            i = i + 1;
        } else {
            myReadFile.getline(cNum, 256, ' ');
            cpu_id = atoi(cNum);
            found = true;
        }
    }

    return cpu_id;
}

void set_cpu_affinity(std::vector<uint32_t> cpus, pid_t pid)
{
    // All cpus allowed
    if (cpus.size() == 0)
        return;

    // Set CPU affinity
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (auto cpu : cpus)
        CPU_SET(cpu, &mask);
    if (sched_setaffinity(pid, sizeof(mask), &mask) < 0)
        throw_with_trace(std::runtime_error("Could not set CPU affinity: " +
                                            std::string(strerror(errno))));
}

void pid_get_children_rec(const pid_t pid, std::vector<pid_t> &children)
{
    std::ifstream proc_children;
    proc_children.open("/proc/{}/task/{}/children"_format(pid, pid));
    pid_t child_pid = -1;
    while (proc_children >> child_pid) {
        children.push_back(child_pid);
        pid_get_children_rec(child_pid, children);
    }
}

double getTemperatureCPU(uint32_t core)
{
    uint32_t node;
    uint32_t core_id;

    if (((core >= 0) & (core <= 11)) | ((core >= 24) & (core <= 35))) {
        node = 0;
    } else {
        node = 1;
    }

    switch (core) {
        case 0:
        case 24:
        case 12:
        case 36:
            core_id = 2;
            break;
        case 1:
        case 25:
        case 13:
        case 37:
            core_id = 3;
            break;
        case 2:
        case 26:
        case 14:
        case 38:
            core_id = 4;
            break;
        case 3:
        case 27:
        case 15:
        case 39:
            core_id = 5;
            break;
        case 4:
        case 28:
        case 16:
        case 40:
            core_id = 6;
            break;
        case 5:
        case 29:
        case 17:
        case 41:
            core_id = 7;
            break;
        case 6:
        case 30:
        case 18:
        case 42:
            core_id = 10;
            break;
        case 7:
        case 31:
        case 19:
        case 43:
            core_id = 11;
            break;
        case 8:
        case 32:
        case 20:
        case 44:
            core_id = 12;
            break;
        case 9:
        case 33:
        case 21:
        case 45:
            core_id = 13;
            break;
        case 10:
        case 34:
        case 22:
        case 46:
            core_id = 14;
            break;
        case 11:
        case 35:
        case 23:
        case 47:
            core_id = 15;
            break;
        default:
            core_id = 1;
            break;
    }

    //LOGINF("Core {} has ID mapping: {}"_format(core, core_id));

    std::string path = "/sys/class/hwmon/hwmon" + std::to_string(node) +
                       "/temp" + std::to_string(core_id) + "_input";
    auto fdata = open_ifstream(path);
    uint64_t data;
    fdata >> data;
    return data / 1000;
}

void ReadStatsCPU(std::vector<CPUData> &entries)
{
    std::ifstream fileStat("/proc/stat");

    std::string line;

    const std::string STR_CPU("cpu");
    const std::size_t LEN_STR_CPU = STR_CPU.size();
    const std::string STR_TOT("tot");

    while (std::getline(fileStat, line)) {
        // cpu stats line found
        if (!line.compare(0, LEN_STR_CPU, STR_CPU)) {
            std::istringstream ss(line);

            // store entry
            entries.emplace_back(CPUData());
            CPUData &entry = entries.back();

            // read cpu label
            ss >> entry.cpu;

            // remove "cpu" from the label when it's a processor number
            if (entry.cpu.size() > LEN_STR_CPU)
                entry.cpu.erase(0, LEN_STR_CPU);
            // replace "cpu" with "tot" when it's total values
            else
                entry.cpu = STR_TOT;

            // read times
            for (int i = 0; i < NUM_CPU_STATES; ++i)
                ss >> entry.times[i];
        }
    }
}

size_t GetIdleTime(const CPUData &e)
{
    return e.times[S_IDLE] + e.times[S_IOWAIT];
}

size_t GetTime(const CPUData &e, std::string time)
{
    CPUStates c;
    std::map<std::string, uint32_t> time_to_int = {
        {"user", 0},   {"nice", 1},      {"system", 2},  {"idle", 3},
        {"iowait", 4}, {"irq", 5},       {"softirq", 6}, {"steal", 7},
        {"guest", 8},  {"guest_nice", 9}};

    switch (time_to_int[time]) {
        case 0:
            c = S_USER;
            break;
        case 1:
            c = S_NICE;
            break;
        case 2:
            c = S_SYSTEM;
            break;
        case 3:
            c = S_IDLE;
            break;
        case 4:
            c = S_IOWAIT;
            break;
        case 5:
            c = S_IRQ;
            break;
        case 6:
            c = S_SOFTIRQ;
            break;
        case 7:
            c = S_STEAL;
            break;
        case 8:
            c = S_GUEST;
            break;
        case 9:
            c = S_GUEST_NICE;
            break;
    }

    return e.times[c];
}

size_t GetActiveTime(const CPUData &e)
{
    return e.times[S_USER] + e.times[S_NICE] + e.times[S_SYSTEM] +
           e.times[S_IRQ] + e.times[S_SOFTIRQ] + e.times[S_STEAL];
}

float get_cpu_utilization(const std::vector<CPUData> &entries1,
                          const std::vector<CPUData> &entries2, uint64_t cpu)
{
    const size_t NUM_ENTRIES = entries1.size();

    for (size_t i = 0; i < NUM_ENTRIES; ++i) {
        const CPUData &e1 = entries1[i];
        const CPUData &e2 = entries2[i];

        if (e1.cpu == std::to_string(cpu)) {
            const float ACTIVE_TIME =
                static_cast<float>(GetActiveTime(e2) - GetActiveTime(e1));
            const float IDLE_TIME =
                static_cast<float>(GetIdleTime(e2) - GetIdleTime(e1));
            const float TOTAL_TIME = ACTIVE_TIME + IDLE_TIME;
            return (100.f * ACTIVE_TIME / TOTAL_TIME);
        }
    }
    return -1;
}

float get_time_utilization(const std::vector<CPUData> &entries1,
                           const std::vector<CPUData> &entries2, uint64_t cpu,
                           std::string time)
{
    const size_t NUM_ENTRIES = entries1.size();

    for (size_t i = 0; i < NUM_ENTRIES; ++i) {
        const CPUData &e1 = entries1[i];
        const CPUData &e2 = entries2[i];

        if (e1.cpu == std::to_string(cpu)) {
            const float TIME =
                static_cast<float>(GetTime(e2, time) - GetTime(e1, time));
            //const float ACTIVE_TIME = static_cast<float>(GetActiveTime(e2) - GetActiveTime(e1));
            //const float IDLE_TIME   = static_cast<float>(GetIdleTime(e2) - GetIdleTime(e1));
            //const float TOTAL_TIME  = ACTIVE_TIME + IDLE_TIME;
            //return (100.f * TIME / TOTAL_TIME);

            return TIME;
        }
    }
    return -1;
}

