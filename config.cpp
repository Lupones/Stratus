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

#include <cstdlib>
#include <iostream>
#include <map>

#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include "config.hpp"
#include "log.hpp"

#ifndef LIBVIRT_HEADERS_H
#define LIBVIRT_HEADERS_H

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#endif

using std::string;
using std::vector;
using fmt::literals::operator""_format;

static std::shared_ptr<cat::policy::Base>
config_read_cat_policy(const YAML::Node &config);
static vector<Cos> config_read_cos(const YAML::Node &config);
static tasklist_t config_read_tasks(const YAML::Node &config);
static YAML::Node merge(YAML::Node user, YAML::Node def);
static void config_check_required_fields(const YAML::Node &node,
                                         const std::vector<string> &required);
static void config_check_fields(const YAML::Node &node,
                                const std::vector<string> &required,
                                std::vector<string> allowed);

static void config_check_required_fields(const YAML::Node &node,
                                         const std::vector<string> &required)
{
    assert(node.IsMap());

    // Check that required fields exist
    for (string field : required)
        if (!node[field])
            throw_with_trace(std::runtime_error(
                "The node '{}' requires the field '{}'"_format(node.Scalar(),
                                                               field)));
}

static void config_check_fields(const YAML::Node &node,
                                const std::vector<string> &required,
                                std::vector<string> allowed)
{
    // Allowed is passed by value...
    allowed.insert(allowed.end(), required.begin(), required.end());

    assert(node.IsMap());

    config_check_required_fields(node, required);

    // Check that all the fields present are allowed
    for (const auto &n : node) {
        string field = n.first.Scalar();
        if (std::find(allowed.begin(), allowed.end(), field) == allowed.end())
            LOGWAR("Field '{}' is not allowed in the '{}' node"_format(
                field, node.Scalar()));
    }
}

static std::shared_ptr<cat::policy::Base>
config_read_cat_policy(const YAML::Node &config)
{
    YAML::Node policy = config["policy"];

    if (!policy["kind"])
        throw_with_trace(
            std::runtime_error("The partitioning policy needs a 'kind' field"));
    string kind = policy["kind"].as<string>();

    if (kind == "none")
        return std::make_shared<cat::policy::Base>();
    else if (kind == "test") {
        LOGINF("Using Test partitioning policy");

        // Check that required fields exist
        for (string field : {"every"}) {
            if (!policy[field])
                throw_with_trace(std::runtime_error("The '" + kind +
                                                    "' policy needs the '" +
                                                    field + "' field"));
        }
        // Read fields
        uint64_t every = policy["every"].as<uint64_t>();

        return std::make_shared<cat::policy::Test>(every);
    } else
        throw_with_trace(
            std::runtime_error("Unknown policy: '" + kind + "'"));
}

static vector<Cos> config_read_cos(const YAML::Node &config)
{
    YAML::Node cos_section = config["clos"];
    auto result = vector<Cos>();

    if (cos_section.Type() != YAML::NodeType::Sequence)
        throw_with_trace(std::runtime_error(
            "In the config file, the clos section must contain a sequence"));

    for (size_t i = 0; i < cos_section.size(); i++) {
        const auto &cos = cos_section[i];
        LOGINF(cos_section[i]);
        auto num = 0;
        auto mask = 0x7ff;
        uint64_t mbps = -1;
        auto cpus = vector<uint32_t>();

        // Schematas are mandatory
        if (!cos["num"])
            throw_with_trace(std::runtime_error("Each clos must have a num"));
        else
            num = cos["num"].as<uint32_t>();

        if (cos["schemata"])
            //throw_with_trace(std::runtime_error("Each clos must have an schemata"));
            mask = cos["schemata"].as<uint64_t>();

        if (cos["mbps"])
            mbps = cos["mbps"].as<int>();

        // CPUs are not mandatory, but note that all the CPUs are assigned to CLOS 0 by default
        if (cos["cpus"]) {
            auto cpulist = cos["cpus"];
            for (auto it = cpulist.begin(); it != cpulist.end(); it++) {
                int cpu = it->as<int>();
                cpus.push_back(cpu);
            }
        }

        result.push_back(Cos(num, mask, mbps, cpus));
        num = num + 1;
    }

    return result;
}

static tasklist_t config_read_tasks(const YAML::Node &config)
{
    YAML::Node tasks = config["tasks"];
    auto result = tasklist_t();
    vector<string> required;
    vector<string> allowed;
    string client_args, arguments, client_arguments;
    int client_num_cpus;
    for (size_t i = 0; i < tasks.size(); i++) {
        if (!tasks[i]["kind"])
            throw_with_trace(
                std::runtime_error("Each Task  needs a 'kind' field to specify "
                                   "if it is a vm or an app."));
        string kind = tasks[i]["kind"].as<string>();

        if (kind == "VM") {
            //template
            required = {"app", "domain_name", "snapshot_name", "ip", "kind"};
            allowed = {"max_restarts",
                       "define",
                       "initial_clos",
                       "cpus",
                       "batch",
                       "client_domain_name",
                       "client_ip",
                       "client_snapshot_name",
                       "port",
                       "arguments",
                       "client_arguments",
                       "disk_total_bytes_sec",
                       "disk_read_bytes_sec",
                       "disk_write_bytes_sec",
                       "disk_total_iops_sec",
                       "disk_read_iops_sec",
                       "disk_write_iops_sec",
                       "netbw_in_avg",
                       "netbw_in_peak",
                       "netbw_in_burst",
                       "netbw_out_avg",
                       "netbw_out_peak",
                       "netbw_out_burst",
                       "client_num_cpus",
                       "client_cpus",
                       "ceph_vm",
                       "client_native"};

            config_check_fields(tasks[i], required, allowed);

            /*** PROCESS APPLICATIONS.MAKO ***/
            if (!tasks[i]["app"])
                throw_with_trace(
                    std::runtime_error("Each task must have an app dictionary "
                                       "with at least the key "
                                       "'cmd', and optionally the keys "
                                       "'stdout', 'stdin', 'stderr'"));

            const auto &app = tasks[i]["app"];
            required = {"client"};
            allowed = {"name",   "stdin",       "stdout",
                       "stderr", "client_args", "args"};

            config_check_fields(app, required, allowed);

            // Arguments line
            string args = app["args"].as<string>();

            // Name defaults to the name of the executable if not provided
            string name = app["name"].as<string>();

            // Client-server model
            if (!app["client"]) {
                throw_with_trace(
                    std::runtime_error("Each task must specify if it is a "
                                       "client-server task or not"));
            }
            const bool client = app["client"].as<bool>();

            if (client)
                client_args = app["client_args"].as<string>();

            /*** PROCESS TEMPLATE.MAKO ***/
            // Domain name
            string domain_name = tasks[i]["domain_name"]
                                     ? tasks[i]["domain_name"].as<string>()
                                     : "error_domain_name";

            // Domain IP
            string domain_ip = tasks[i]["ip"] ? tasks[i]["ip"].as<string>()
                                              : "error_domain_ip";

            /*if (!tasks[i]["port"]) {
                    throw_with_trace(std::runtime_error(
                            "Client-server benchmarks must specify the port"));
                    }*/
            string port =
                tasks[i]["port"] ? tasks[i]["port"].as<string>() : NULL;

            // Snapshot name
            string snapshot_name = tasks[i]["snapshot_name"]
                                       ? tasks[i]["snapshot_name"].as<string>()
                                       : "error_snapshot_name";

            // Disk throttling config
            disk_config dc;
            dc.total_bytes_sec =
                tasks[i]["disk_total_bytes_sec"]
                    ? tasks[i]["disk_total_bytes_sec"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            dc.read_bytes_sec =
                tasks[i]["disk_read_bytes_sec"]
                    ? tasks[i]["disk_read_bytes_sec"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            dc.write_bytes_sec =
                tasks[i]["disk_write_bytes_sec"]
                    ? tasks[i]["disk_write_bytes_sec"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            dc.total_iops_sec =
                tasks[i]["disk_total_iops_sec"]
                    ? tasks[i]["disk_total_iops_sec"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            dc.read_iops_sec =
                tasks[i]["disk_read_iops_sec"]
                    ? tasks[i]["disk_read_iops_sec"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            dc.write_iops_sec =
                tasks[i]["disk_write_iops_sec"]
                    ? tasks[i]["disk_write_iops_sec"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);

            // Network limitation parameters
            long long netbw_in_avg =
                tasks[i]["netbw_in_avg"]
                    ? tasks[i]["netbw_in_avg"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            long long netbw_in_burst =
                tasks[i]["netbw_in_burst"]
                    ? tasks[i]["netbw_in_burst"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            long long netbw_in_peak =
                tasks[i]["netbw_in_peak"]
                    ? tasks[i]["netbw_in_peak"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            long long netbw_out_avg =
                tasks[i]["netbw_out_avg"]
                    ? tasks[i]["netbw_out_avg"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            long long netbw_out_burst =
                tasks[i]["netbw_out_burst"]
                    ? tasks[i]["netbw_out_burst"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);
            long long netbw_out_peak =
                tasks[i]["netbw_out_peak"]
                    ? tasks[i]["netbw_out_peak"].as<unsigned long long>()
                    : std::stoull("0", NULL, 10);

            // Client-server specific parameters
            //string port = "NULL";
            string client_domain_name = "fake_client_domain_name",
                   client_domain_ip = "fake_client_domain_ip",
                   client_snapshot_name = "fake_client_snapshot_name",
                   client_cmd = "fake_client_cmd";

            bool client_native = tasks[i]["client_native"]
                                     ? tasks[i]["client_native"].as<bool>()
                                     : false;
            LOGINF(client_native);

            // Args that can be modified
            arguments = tasks[i]["arguments"].as<string>();

            if (client) {
                // Client Args
                client_arguments = tasks[i]["client_arguments"].as<string>();

                // Client num cpus
                client_num_cpus = tasks[i]["client_num_cpus"]
                                      ? tasks[i]["client_num_cpus"].as<int>()
                                      : std::stoi("1");
                LOGINF(
                    " +++++ Domain: {} Snapshot: {} Client-server? {} Name {} +++++"_format(
                        domain_name, snapshot_name, client, name));

                if (!client_native) {
                    if (!tasks[i]["client_domain_name"]) {
                        throw_with_trace(std::runtime_error(
                            "Task {} of type client-server but missing client_domain_name."_format(
                                name)));
                    }
                    client_domain_name =
                        tasks[i]["client_domain_name"].as<string>();

                    if (!tasks[i]["client_ip"]) {
                        throw_with_trace(std::runtime_error(
                            "Task {} of type client-server but missing client_domain_ip."_format(
                                name)));
                    }
                    client_domain_ip = tasks[i]["client_ip"].as<string>();

                    if (!tasks[i]["client_snapshot_name"]) {
                        throw_with_trace(std::runtime_error(
                            "Task {} of type client-server but missing client_snapshot_name."_format(
                                name)));
                    }
                    client_snapshot_name =
                        tasks[i]["client_snapshot_name"].as<string>();
                    LOGINF(
                        " +++++ CLIENT -- Domain: {} Snapshot: {} IP: {} +++++"_format(
                            client_domain_name, client_snapshot_name,
                            client_domain_ip));
                }
            }

            // virConnectPtr
            // I thought that the manager would create and open it, and then pass it to the tasks, but for now it is
            const int connect_flags = 0;
            virConnectPtr conn;
            conn = virConnectOpenAuth(
                "qemu:///system", virConnectAuthPtrDefault,
                connect_flags); // For now, the URI is directly coded here
            if (conn == NULL) {
                LOGINF(
                    "***** ERROR: failed to connect to the hypervisor *****");
                exit(0);
            }

            // Stdin/out/err redirection
            string output = app["stdout"] ? app["stdout"].as<string>() : "out";
            string input = app["stdin"] ? app["stdin"].as<string>() : "";
            string error = app["stderr"] ? app["stderr"].as<string>() : "err";

            // Server CPU affinity
            auto cpus = vector<uint32_t>();
            if (tasks[i]["cpus"]) {
                auto node = tasks[i]["cpus"];
                assert(node.IsScalar() || node.IsSequence());
                if (node.IsScalar())
                    cpus = {node.as<decltype(cpus)::value_type>()};
                else
                    cpus = node.as<decltype(cpus)>();
            }

            // Client CPU affinity
            auto client_cpus = vector<uint32_t>();
            if (tasks[i]["client_cpus"]) {
                auto node = tasks[i]["client_cpus"];
                assert(node.IsScalar() || node.IsSequence());
                if (node.IsScalar())
                    client_cpus = {node.as<decltype(cpus)::value_type>()};
                else
                    client_cpus = node.as<decltype(cpus)>();
            }

            // Initial CLOS
            uint32_t initial_clos =
                tasks[i]["initial_clos"]
                    ? tasks[i]["initial_clos"].as<decltype(initial_clos)>()
                    : 0;
            LOGINF("Initial CLOS {}"_format(initial_clos));

            // String to string replacement, a la C preprocesor, in the 'cmd' option
            auto vars = std::map<string, string>();
            if (tasks[i]["define"]) {
                auto node = tasks[i]["define"];
                try {
                    vars = node.as<decltype(vars)>();
                } catch (const std::exception &e) {
                    throw_with_trace(std::runtime_error(
                        "The option 'define' should contain a "
                        "string to string mapping"));
                }

                for (auto it = vars.begin(); it != vars.end(); ++it) {
                    string key = it->first;
                    string value = it->second;
                }
            }

            // Maximum number of restarts of an application
            uint32_t max_restarts =
                tasks[i]["max_restarts"]
                    ? tasks[i]["max_restarts"].as<decltype(max_restarts)>()
                    : std::numeric_limits<decltype(max_restarts)>::max();

            bool batch =
                tasks[i]["batch"] ? tasks[i]["batch"].as<bool>() : false;

            bool ceph_vm =
                tasks[i]["ceph_vm"] ? tasks[i]["ceph_vm"].as<bool>() : false;

            result.push_back(std::make_shared<VMTask>(
                name, cpus, initial_clos, output, input, error, max_restarts,
                batch, client, conn, domain_name, domain_ip, port,
                snapshot_name, ceph_vm, client_native, dc, client_domain_name,
                client_domain_ip, client_snapshot_name, client_num_cpus,
                client_cpus, args, client_args, arguments, client_arguments,
                netbw_in_avg, netbw_in_peak, netbw_in_burst, netbw_out_avg,
                netbw_out_peak, netbw_out_burst));

        } else if (kind == "app") {
            required = {"app", "kind"};
            allowed = {"max_instr",    "max_restarts", "define",
                       "initial_clos", "cpus",         "batch"};
            config_check_fields(tasks[i], required, allowed);

            /*** PROCESS APPLICATIONS.MAKO ***/
            if (!tasks[i]["app"])
                throw_with_trace(std::runtime_error(
                    "Each task must have an app dictionary with at least the "
                    "key 'cmd', and optionally the keys 'stdout', "
                    "'stdin','stderr', 'skel' and 'max_instr'"));

            const auto &app = tasks[i]["app"];

            required = {"cmd", "client"};
            allowed = {"name", "skel", "stdin", "stdout", "stderr"};
            config_check_fields(app, required, allowed);

            // Commandline and name
            if (!app["cmd"])
                throw_with_trace(
                    std::runtime_error("Each task must have a cmd"));
            string cmd = app["cmd"].as<string>();

            // Name defaults to the name of the executable if not provided
            string name = app["name"] ? app["name"].as<string>()
                                      : extract_executable_name(cmd);

            // Client-server model
            if (!app["client"]) {
                throw_with_trace(
                    std::runtime_error("Each task must specify if it is a "
                                       "client-server task or not"));
            }
            const bool client = app["client"].as<bool>();

            // Dir containing files to copy to rundir
            vector<string> skel = {""};
            if (app["skel"]) {
                if (app["skel"].IsSequence())
                    skel = app["skel"].as<vector<string>>();
                else
                    skel = {app["skel"].as<string>()};
            }

            // Stdin/out/err redirection
            string output = app["stdout"] ? app["stdout"].as<string>() : "out";
            string input = app["stdin"] ? app["stdin"].as<string>() : "";
            string error = app["stderr"] ? app["stderr"].as<string>() : "err";

            /*** PROCESS TEMPLATE.MAKO ***/
            // CPU affinity
            auto cpus = vector<uint32_t>();
            if (tasks[i]["cpus"]) {
                auto node = tasks[i]["cpus"];
                assert(node.IsScalar() || node.IsSequence());
                if (node.IsScalar())
                    cpus = {node.as<decltype(cpus)::value_type>()};
                else
                    cpus = node.as<decltype(cpus)>();
            }

            // Initial CLOS
            uint32_t initial_clos =
                tasks[i]["initial_clos"]
                    ? tasks[i]["initial_clos"].as<decltype(initial_clos)>()
                    : 0;
            LOGINF("Initial CLOS {}"_format(initial_clos));

            // String to string replacement, a la C preprocesor, in the 'cmd' option
            auto vars = std::map<string, string>();
            if (tasks[i]["define"]) {
                auto node = tasks[i]["define"];
                try {
                    vars = node.as<decltype(vars)>();
                } catch (const std::exception &e) {
                    throw_with_trace(
                        std::runtime_error("The option 'define' should contain "
                                           "a string to string mapping"));
                }

                for (auto it = vars.begin(); it != vars.end(); ++it) {
                    string key = it->first;
                    string value = it->second;
                    boost::replace_all(cmd, key, value);
                }
            }

            // Maximum number of instructions to execute
            auto max_instr = tasks[i]["max_instr"]
                                 ? tasks[i]["max_instr"].as<uint64_t>()
                                 : 0;
            uint32_t max_restarts =
                tasks[i]["max_restarts"]
                    ? tasks[i]["max_restarts"].as<decltype(max_restarts)>()
                    : std::numeric_limits<decltype(max_restarts)>::max();

            bool batch =
                tasks[i]["batch"] ? tasks[i]["batch"].as<bool>() : false;

            result.push_back(std::make_shared<AppTask>(
                name, cpus, initial_clos, output, input, error, max_restarts,
                batch, client, cmd, skel, max_instr));
        }
    }
    return result;
}

static YAML::Node merge(YAML::Node user, YAML::Node def)
{
    if (user.Type() == YAML::NodeType::Map &&
        def.Type() == YAML::NodeType::Map) {
        for (auto it = def.begin(); it != def.end(); ++it) {
            std::string key = it->first.Scalar();
            YAML::Node value = it->second;

            if (!user[key])
                user[key] = value;
            else
                user[key] = merge(user[key], value);
        }
    }
    return user;
}

static void config_read_cmd_options(const YAML::Node &config,
                                    CmdOptions &cmd_options)
{
    if (!config["cmd"])
        return;

    const auto &cmd = config["cmd"];

    vector<string> required;
    vector<string> allowed;

    required = {};
    allowed = {"ti", "mi", "event", "cpu-affinity", "perf"};

    // Check minimum required fields
    config_check_fields(cmd, required, allowed);

    if (cmd["ti"])
        cmd_options.ti = cmd["ti"].as<decltype(cmd_options.ti)>();
    if (cmd["mi"])
        cmd_options.mi = cmd["mi"].as<decltype(cmd_options.mi)>();
    if (cmd["event"])
        cmd_options.event = cmd["event"].as<decltype(cmd_options.event)>();
    if (cmd["perf"])
        cmd_options.perf = cmd["perf"].as<decltype(cmd_options.perf)>();
    if (cmd["cpu-affinity"])
        cmd_options.cpu_affinity =
            cmd["cpu-affinity"].as<decltype(cmd_options.cpu_affinity)>();
}

void config_read(const string &path, const string &overlay,
                 CmdOptions &cmd_options, tasklist_t &tasklist,
                 vector<Cos> &coslist,
                 std::shared_ptr<cat::policy::Base> &catpol)
{
    // The message outputed by YAML is not clear enough, so we test first
    std::ifstream f(path);
    if (!f.good())
        throw_with_trace(
            std::runtime_error("File doesn't exist or is not readable"));

    YAML::Node config = YAML::LoadFile(path);

    if (overlay != "") {
        YAML::Node over = YAML::Load(overlay);
        config = merge(over, config);
    }

    // Read initial CAT config
    if (config["clos"])
        coslist = config_read_cos(config);

    // Read partitioning policy
    if (config["policy"])
        catpol = config_read_cat_policy(config);

    LOGINF("Going to read tasks...");

    // Read tasks into objects
    if (config["tasks"])
        tasklist = config_read_tasks(config);

    // Check that all CLOS (but 0) have cpus or tasks assigned
    for (size_t i = 1; i < coslist.size(); i++) {
        const auto &cos = coslist[i];
        if (cos.cpus.empty())
            std::cerr << "Warning: CLOS " + std::to_string(i) +
                             " has no assigned CPUs"
                      << std::endl;
    }

    // Read general config
    config_read_cmd_options(config, cmd_options);
}
