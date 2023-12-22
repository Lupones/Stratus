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

#include "log.hpp"
#include "throw-with-trace.hpp"
#include "vm-task.hpp"
#include <boost/algorithm/string.hpp>
#include <cxx-prettyprint/prettyprint.hpp>
#include <fmt/format.h>
#include <iostream>
#include <libvirt/libvirt.h>
#include <pstream.h>
#include <stdlib.h>
#include <string>

typedef struct _virNetDevBandwidthRate virNetDevBandwidthRate;
typedef virNetDevBandwidthRate *virNetDevBandwidthRatePtr;
struct _virNetDevBandwidthRate {
    unsigned long long average; /* kbytes/s */
    unsigned long long peak;    /* kbytes/s */
    unsigned long long floor;   /* kbytes/s */
    unsigned long long burst;   /* kbytes */
};

enum open_target { MGMT, SNOOP };

void net_getBwBytes(const VMTask &task, long long *rx_bytes,
                    long long *tx_bytes);
void net_getBridgeInterface(VMTask &task);
void net_setBwLimit(const VMTask &task, unsigned long long inbound_avg,
                    unsigned long long inbound_peak,
                    unsigned long long inbound_burst,
                    unsigned long long outbound_avg,
                    unsigned long long outbound_peak,
                    unsigned long long outbound_burst);

void ovs_ofctl_poll_stats(std::string domain, double *rx_bytes, double *tx_bytes);

