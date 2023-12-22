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

#include "disk-utils.hpp"
#include "log.hpp"
#include <cstdio>
#include <iostream>

#include "throw-with-trace.hpp"

using fmt::literals::operator""_format;

#define N_(String) String

struct _domblkstat_sequence {
    const char *field;  // field name
    const char *legacy; // legacy name from previous releases
    const char *human;  // human-friendly explanation
};

// sequence of values for output to honor legacy format from previous versions
static const struct _domblkstat_sequence domblkstat_output[] = {
    {VIR_DOMAIN_BLOCK_STATS_READ_REQ, "rd_req",
     N_("number of read operations:")}, // 0
    {VIR_DOMAIN_BLOCK_STATS_READ_BYTES, "rd_bytes",
     N_("number of bytes read:")}, // 1
    {VIR_DOMAIN_BLOCK_STATS_WRITE_REQ, "wr_req",
     N_("number of write operations:")}, // 2
    {VIR_DOMAIN_BLOCK_STATS_WRITE_BYTES, "wr_bytes",
     N_("number of bytes written:")},                          // 3
    {VIR_DOMAIN_BLOCK_STATS_ERRS, "errs", N_("error count:")}, // 4
    {VIR_DOMAIN_BLOCK_STATS_FLUSH_REQ, NULL,
     N_("number of flush operations:")}, // 5
    {VIR_DOMAIN_BLOCK_STATS_READ_TOTAL_TIMES, NULL,
     N_("total duration of reads (ns):")}, // 6
    {VIR_DOMAIN_BLOCK_STATS_WRITE_TOTAL_TIMES, NULL,
     N_("total duration of writes (ns):")}, // 7
    {VIR_DOMAIN_BLOCK_STATS_FLUSH_TOTAL_TIMES, NULL,
     N_("total duration of flushes (ns):")}, // 8
    {NULL, NULL, NULL}};

/* Return a non-NULL string representation of a typed parameter; exit
 * if we are out of memory.  */
char *vshGetTypedParamValue(virTypedParameterPtr item)
{
    int ret = 0;
    char *str = NULL;

    switch (item->type) {
        case VIR_TYPED_PARAM_INT:
            ret = asprintf(&str, "%d", item->value.i);
            break;

        case VIR_TYPED_PARAM_UINT:
            ret = asprintf(&str, "%u", item->value.ui);
            break;

        case VIR_TYPED_PARAM_LLONG:
            ret = asprintf(&str, "%lld", item->value.l);
            break;

        case VIR_TYPED_PARAM_ULLONG:
            ret = asprintf(&str, "%llu", item->value.ul);
            break;

        case VIR_TYPED_PARAM_DOUBLE:
            ret = asprintf(&str, "%f", item->value.d);
            break;

        case VIR_TYPED_PARAM_BOOLEAN:
            if (item->value.b) {
                str = strdup("yes");
            } else {
                str = strdup("no");
            }
            break;

        case VIR_TYPED_PARAM_STRING:
            str = strdup(item->value.s);
            break;

        default:
            LOGINF("disk-utils: unimplemented parameter type %d"_format(
                item->type));
    }

    if (ret < 0) {
        LOGINF("disk-utils: Out of memory");
        exit(-1);
    }
    return str;
}

void DiskUtils::update_stats(int i, unsigned long value)
{
    //LOGINF("Update_stats {} {}"_format(i, value));
    //LOGINF("--> {} {} {}"_format(disk_stats[i][0], disk_stats[i][1], disk_stats[i][2]));

    // Move current to last
    disk_stats[i][1] = disk_stats[i][0];

    // Update current
    disk_stats[i][0] = value;

    // Update overall
    if (disk_stats[i][1] > 0) {
        disk_stats[i][2] += disk_stats[i][0] - disk_stats[i][1];
    }

    //LOGINF("<-- {} {} {}"_format(disk_stats[i][0], disk_stats[i][1], disk_stats[i][2]));

    disk_stats[i][3] = value;
}

void DiskUtils::print_disk_stats(const virDomainPtr dom,
                                 uint64_t interval_length_us, uint32_t interval)
{
    double mb_read = (double)disk_stats[1][2] / 1024 / 1024;
    double mb_write = (double)disk_stats[3][2] / 1024 / 1024;
    double time = (double)interval_length_us / 1000 / 1000 * interval;
    double read_bw = mb_read / time;
    double write_bw = mb_write / time;

    // LOGINF("DiskUtils -- Time: {}s"_format(time));

    LOGINF(
        "DiskUtils -- Final READ -- Ops: {} Bytes: {} Time(ns): {} BW(MB/s): {}"_format(
            disk_stats[0][2], disk_stats[1][2], disk_stats[6][2], read_bw));
    LOGINF(
        "DiskUtils -- Final WRITE -- Ops: {} Bytes: {} Times(ns): {} BW(MB/s): {}"_format(
            disk_stats[2][2], disk_stats[3][2], disk_stats[7][2], write_bw));
    LOGINF("DiskUtils -- Final FLUSHES -- Ops: {} Time(ns): {}"_format(
        disk_stats[5][2], disk_stats[8][2]));
}

void DiskUtils::print_disk_stats_quantum(const virDomainPtr dom, int64_t delay)
{
    double mb_read =
        (double)(disk_stats[1][0] - disk_stats[1][1]) / 1024 / 1024;
    double mb_write =
        (double)(disk_stats[3][0] - disk_stats[3][1]) / 1024 / 1024;
    double time = (double)delay / 1000 / 1000;
    double read_bw = mb_read / time;
    double write_bw = mb_write / time;

    /*LOGINF(
        "DiskUtils -- Quantum READ -- Ops: {} Bytes: {} Time(ns): {} BW(MB/s): {}"_format(
            disk_stats[0][0] - disk_stats[0][1],
            disk_stats[1][0] - disk_stats[1][1],
            disk_stats[6][0] - disk_stats[6][1], read_bw));
    LOGINF(
        "DiskUtils -- Quantum WRITE -- Ops: {} Bytes: {} Time(ns): {} BW(MB/s): {}"_format(
            disk_stats[2][0] - disk_stats[2][1],
            disk_stats[3][0] - disk_stats[3][1],
            disk_stats[7][0] - disk_stats[7][1], write_bw));
    LOGINF("DiskUtils -- Quantum FLUSHES -- Ops: {} Time(ns): {}"_format(
        disk_stats[5][0] - disk_stats[5][1],
        disk_stats[8][0] - disk_stats[8][1]));
	*/
}

//static bool cmdDomblkstat(vshControl *ctl, const vshCmd *cmd) {
bool DiskUtils::read_disk_stats(virDomainPtr dom)
{
    const char *name = NULL, *device = NULL;
    virTypedParameterPtr params = NULL;
    virTypedParameterPtr par = NULL;
    char *value = NULL;
    const char *field = NULL;
    int rc, nparams = 0;
    size_t i;
    bool ret = false;

    device = "";

    rc = virDomainBlockStatsFlags(dom, device, NULL, &nparams, 0);

    // LOGINF("disk-utils: Params is {}"_format(nparams));

    // This is the error case
    if (rc < 0) {
        LOGINF("disk-utils: This is the error case. Usually caused by the VM "
               "exiting before we call this function. Seems innocuous");
        //exit (-1);
    }

    // This is the success case
    else {
        //    params = vshCalloc(ctl, nparams, sizeof(*params));
        params =
            (virTypedParameterPtr)malloc(nparams * sizeof(virTypedParameter));
        if (params == NULL) {
            LOGINF(
                "Error in cmdDomblkstat when allocating the memory for params");
            exit(-1);
        }

        if (virDomainBlockStatsFlags(dom, device, params, &nparams, 0) < 0) {
            LOGINF("Failed to get block stats for domain {} device {}"_format(
                name, device));
            goto cleanup;
        }

        // at first print all known values in desired order
        for (i = 0; domblkstat_output[i].field != NULL; i++) {
            if (!(par = virTypedParamsGet(params, nparams,
                                          domblkstat_output[i].field))) {
                //LOGINF("Continue {}"_format(domblkstat_output[i].field));
                continue;
            }

            value = vshGetTypedParamValue(par);

            // to print other not supported fields, mark the already printed
            par->field[0] = '\0'; // set the name to empty string

            // translate into human readable or legacy spelling
            field = NULL;

            //field = _(domblkstat_output[i].human);
            field = strdup(domblkstat_output[i].human);

            // use the provided spelling if no translation is available
            if (!field)
                field = domblkstat_output[i].field;

            //vshPrint(ctl, "%s %-*s %s\n", device, human ? 31 : 0, field, value);
            //LOGINF("disk-utils: {} {} {}"_format(device, field, value));

            update_stats((int)i, strtoul(value, NULL, 0));
            free(value);
        }
    }

    ret = true;

cleanup:
    free(params);
    return ret;
}

// fast access to disk bw (KB/s)
double DiskUtils::get_disk_bw_quantum(int64_t delay)
{
    double mb_read =
        (double)(disk_stats[1][0] - disk_stats[1][1]) / 1024 / 1024;
    double mb_write =
        (double)(disk_stats[3][0] - disk_stats[3][1]) / 1024 / 1024;
    double time = (double)delay / 1000 / 1000;
    double read_bw = mb_read / time;
    double write_bw = mb_write / time;


   return (read_bw + write_bw) / 1000;
}

double DiskUtils::get_disk_rd_bw_quantum(int64_t delay)
{
    double mb_read =
        (double)(disk_stats[1][0] - disk_stats[1][1]) / 1024;
    double time = (double)delay / 1000 / 1000;
    double read_bw = mb_read / time;

   return read_bw;
}
double DiskUtils::get_disk_wr_bw_quantum(int64_t delay)
{
    double mb_write =
        (double)(disk_stats[3][0] - disk_stats[3][1]) / 1024;
    double time = (double)delay / 1000 / 1000;
    double write_bw = mb_write / time;

   return write_bw;
}


// specifies total throughput limit in bytes per second
void DiskUtils::set_total_bytes_sec(const unsigned long long i)
{
    total_bytes_sec_limit = i;
}

// specifies read throughput limit in bytes per second
void DiskUtils::set_read_bytes_sec(const unsigned long long i)
{
    read_bytes_sec_limit = i;
}

// specifies write throughput limit in bytes per second
void DiskUtils::set_write_bytes_sec(const unsigned long long i)
{
    write_bytes_sec_limit = i;
}

// specifies total I/O operations limit per second
void DiskUtils::set_total_iops_sec(const unsigned long long i)
{
    total_iops_sec_limit = i;
}

// specifies total I/O operations limit per second
void DiskUtils::set_read_iops_sec(const unsigned long long i)
{
    read_iops_sec_limit = i;
}

// specifies write I/O operations limit per second
void DiskUtils::set_write_iops_sec(const unsigned long long i)
{
    write_iops_sec_limit = i;
}

//Based on static bool cmdBlkdeviotune(vshControl *ctl, const vshCmd *cmd);
void DiskUtils::apply_disk_util_limits(const virDomainPtr dom)
{
    const char *disk;
    int nparams = 0;
    int maxparams = 0;
    virTypedParameterPtr params = NULL;
    unsigned int flags = VIR_DOMAIN_AFFECT_CURRENT;

    // Affect only the VM while it is live
    flags |= VIR_DOMAIN_AFFECT_LIVE;

    disk = strdup("vda");

    // Add the required limits to params
    if (total_bytes_sec_limit > 0) {
        LOGINF("Dik-utils: total-bytes-sec st to {}"_format(
            total_bytes_sec_limit));
        if (virTypedParamsAddULLong(&params, &nparams, &maxparams,
                                    VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_BYTES_SEC,
                                    total_bytes_sec_limit) < 0) {
            LOGINF("virTypedParamsAddULLong error. We were trying to set the "
                   "VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_BYTES_SEC");
            exit(0);
        }
    }

    if (read_bytes_sec_limit > 0) {
        LOGINF(
            "Dik-utils: read-bytes-sec st to {}"_format(read_bytes_sec_limit));
        if (virTypedParamsAddULLong(&params, &nparams, &maxparams,
                                    VIR_DOMAIN_BLOCK_IOTUNE_READ_BYTES_SEC,
                                    read_bytes_sec_limit) < 0) {
            LOGINF("virTypedParamsAddULLong error. We were trying to set the "
                   "VIR_DOMAIN_BLOCK_IOTUNE_READ_BYTES_SEC");
            exit(0);
        }
    }

    if (write_bytes_sec_limit > 0) {
        LOGINF("Dik-utils: write-bytes-sec st to {}"_format(
            write_bytes_sec_limit));
        if (virTypedParamsAddULLong(&params, &nparams, &maxparams,
                                    VIR_DOMAIN_BLOCK_IOTUNE_WRITE_BYTES_SEC,
                                    write_bytes_sec_limit) < 0) {
            LOGINF("virTypedParamsAddULLong error. We were trying to set the "
                   "VIR_DOMAIN_BLOCK_IOTUNE_WRITE_BYTES_SEC");
            exit(0);
        }
    }

    if (total_iops_sec_limit > 0) {
        LOGINF(
            "Dik-utils: total-iops-sec st to {}"_format(total_iops_sec_limit));
        if (virTypedParamsAddULLong(&params, &nparams, &maxparams,
                                    VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_IOPS_SEC,
                                    total_iops_sec_limit) < 0) {
            LOGINF("virTypedParamsAddULLong error. We were trying to set the "
                   "VIR_DOMAIN_BLOCK_IOTUNE_TOTAL_IOPS_SEC");
            exit(0);
        }
    }

    if (read_iops_sec_limit > 0) {
        LOGINF("Dik-utils: read-iops-sec st to {}"_format(read_iops_sec_limit));
        if (virTypedParamsAddULLong(&params, &nparams, &maxparams,
                                    VIR_DOMAIN_BLOCK_IOTUNE_READ_IOPS_SEC,
                                    read_iops_sec_limit) < 0) {
            LOGINF("virTypedParamsAddULLong error. We were trying to set the "
                   "VIR_DOMAIN_BLOCK_IOTUNE_READ_IOPS_SEC");
            exit(0);
        }
    }

    if (write_iops_sec_limit > 0) {
        LOGINF(
            "Dik-utils: write-iops-sec st to {}"_format(write_iops_sec_limit));
        if (virTypedParamsAddULLong(&params, &nparams, &maxparams,
                                    VIR_DOMAIN_BLOCK_IOTUNE_WRITE_IOPS_SEC,
                                    write_iops_sec_limit) < 0) {
            LOGINF("virTypedParamsAddULLong error. We were trying to set the "
                   "VIR_DOMAIN_BLOCK_IOTUNE_WRITE_IOPS_SEC");
            exit(0);
        }
    }

    if (nparams > 0) {
        if (virDomainSetBlockIoTune(dom, disk, params, nparams, flags) < 0) {
            LOGINF("--> virDomainSetBlockIoTune error! Unable to change block "
                   "I/O throttle");
            exit(0);
        }
    }

    free(params);
}

