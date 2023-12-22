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

#include "net-bandwidth.hpp"

void net_getBwBytes(const VMTask &task, long long *rx_bytes,
                    long long *tx_bytes)
{
    virDomainPtr dom = task.dom;
    virDomainInterfaceStatsStruct stats;

    if (virDomainInterfaceStats(dom, task.network_interface, &stats,
                                sizeof(stats)) == -1) {
        // throw_with_trace(std::runtime_error("ERROR! Unable to get domain interface stats when trying to get network bandwidth of the VM " +
        //                                   task.domain_name + " with interface " + task.network_interface + "."));
        LOGINF("WARNING: Can't get domain interface stats of " +
               task.domain_name + " with mac " + task.network_interface +
               ". VM could be off!");
    }

    if (stats.rx_bytes >= 0)
        *rx_bytes = stats.rx_bytes;
	else
		*rx_bytes = 0;
    if (stats.tx_bytes >= 0)
        *tx_bytes = stats.tx_bytes;
	else
		*tx_bytes = 0;
}

void net_getBridgeInterface(VMTask &task)
{
    std::string command = "sudo virsh domiflist " + task.domain_name +
                          " | grep \"vhostuser\" | awk '{print $5}'";

    LOGINF(command);
    FILE *fp;
    char path[1035];

    /* Open the command for reading. */
    fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getBridgeInterface function: Failed to run command"));
    }

    /* Read the output a line at a time - output it. */
    if (fgets(path, sizeof(path) - 1, fp) != NULL) {
        strcpy(task.network_interface, path);
    } else {
        throw_with_trace(
            std::runtime_error("***net_getBridgeInterface function: Failed to "
                               "read command output"));
    }

    /* close */
    pclose(fp);

    LOGINF("Network interface (mac) for " + task.domain_name + " is " +
           task.network_interface);
}


void net_setBwLimit(const VMTask &task, unsigned long long inbound_avg,
                    unsigned long long inbound_peak,
                    unsigned long long inbound_burst,
                    unsigned long long outbound_avg,
                    unsigned long long outbound_peak,
                    unsigned long long outbound_burst)
{
    virNetDevBandwidthRate inbound, outbound;

	FILE *fp;

    memset(&inbound, 0, sizeof(inbound));
    memset(&outbound, 0, sizeof(outbound));

    inbound.average = inbound_avg;
    inbound.burst = inbound_burst;
    inbound.peak = inbound_peak;

    outbound.average = outbound_avg;
    outbound.burst = outbound_burst;
    outbound.peak = outbound_peak;

	std::string command;

	/* OVS ifaces names */
	// std::string command = "sudo ovs-vsctl list interface | grep \"name \" | awk '{print $3}'";
	// In our case, assume they are called dpdk0 and dpdk1

	/* INBOUND: Input bounds are set by limiting the ingress rate and burst in the client interfaces */

    /* Inbound ingress rate in dpdk0 */
	command = std::string("sudo ovs-vsctl set interface dpdk0 ingress_policing_rate=" + std::to_string(inbound.average));
    LOGINF(command);

	fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getMACInterface function: Failed to run command"));
    }

    pclose(fp);

    /* Inbound ingress rate in dpdk1 */
	command = std::string("sudo ovs-vsctl set interface dpdk1 ingress_policing_rate=" + std::to_string(inbound.average));
	LOGINF(command);

    fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getMACInterface function: Failed to run command"));
    }

    pclose(fp);

    /* Inbound ingress burst in dpdk0 */
 	command = std::string("sudo ovs-vsctl set interface dpdk0 ingress_policing_burst=" + std::to_string(inbound.burst));
    LOGINF(command);

	fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getMACInterface function: Failed to run command"));
    }

    pclose(fp);

    /* Inbound ingress burst in dpdk1 */
	command = std::string("sudo ovs-vsctl set interface dpdk1 ingress_policing_burst=" + std::to_string(inbound.burst));
	LOGINF(command);

	fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getMACInterface function: Failed to run command"));
    }

    pclose(fp);

	/* OUTBOUND: Output bounds are set by limiting the ingress rate and burst in the server interface */
	// Get vhost name
	const char delim = '_';
	std::vector<std::string> out;
	std::stringstream ss(task.domain_name);
	std::string s;
	while (std::getline(ss,s,delim))
		out.push_back(s);

	std::string domain_2 = out.back();
	out.pop_back();
	domain_2 = out.back() + "-" + domain_2;


	/* Outbound ingress rate in vhost-user1 */
	command = std::string("sudo ovs-vsctl set interface vhost-" + domain_2 + " ingress_policing_rate=" + std::to_string(outbound.average));
	LOGINF(command);

    fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getMACInterface function: Failed to run command"));
    }

    pclose(fp);

	/* Outbound ingress burst in vhost-user1 */
	command = std::string("sudo ovs-vsctl set interface vhost-" + domain_2 +  " ingress_policing_burst=" + std::to_string(outbound.burst));
	LOGINF(command);

    fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        throw_with_trace(std::runtime_error(
            "***net_getMACInterface function: Failed to run command"));
    }

    pclose(fp);
}

void ovs_ofctl_poll_stats(std::string domain, double *rx_bytes, double *tx_bytes)
{
    // Get vhost name
	const char delim = '_';
	std::vector<std::string> out;
	std::stringstream ss(domain);
	std::string s;
	while (std::getline(ss,s,delim))
		out.push_back(s);

	std::string domain_2 = out.back();
	out.pop_back();
	domain_2 = out.back() + "-" + domain_2;

	std::string command = "ovs-ofctl dump-ports ovs_br0 vhost-"+ domain_2;

    // run a process and create a streambuf that reads its stdout and stderr
    redi::ipstream proc(command,
                        redi::pstreams::pstdout | redi::pstreams::pstderr);
    std::string line;

    //LOGINF("### OVS STATS ###");
    // read child's stdout
    while (std::getline(proc.out(), line)) {
        //LOGINF(line);

		// Subtract RX Bytes
        if (line.find("rx") != std::string::npos) {
            std::vector<std::string> results;
            boost::split(results, line, [](char c) { return c == ' '; });
            for (uint32_t i = 0; i < results.size(); i++) {
                std::string aux = results[i];
                if (results[i].find("bytes") != std::string::npos) {
                    aux.erase(0, 6);
                    aux.erase(aux.end() - 1);
                    *rx_bytes = atof(aux.c_str());
					if ((rx_bytes < 0) || (rx_bytes != rx_bytes))
						*rx_bytes = 0;
                }
            }
		// Subtract TX Bytes
        } else if (line.find("tx") != std::string::npos) {
            std::vector<std::string> results;
            boost::split(results, line, [](char c) { return c == ' '; });
            for (uint32_t i = 0; i < results.size(); i++) {
                std::string aux = results[i];
                if (results[i].find("bytes") != std::string::npos) {
                    aux.erase(0, 6);
                    aux.erase(aux.end() - 1);
                    *tx_bytes = atof(aux.c_str());
					if ((tx_bytes < 0) || (tx_bytes != tx_bytes))
						*tx_bytes = 0;
                }
            }
        }
    }
}

