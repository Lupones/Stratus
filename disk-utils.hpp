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

#include <cstdio>
#include <fmt/format.h>

#include "log.hpp"
#include "throw-with-trace.hpp"

#ifndef LIBVIRT_HEADERS_H
#define LIBVIRT_HEADERS_H

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#endif

#define N_PARAMS 9
#define M 4

// M 0 -> last quantum
// M 1 -> quantum before the last one
// M 2 -> overall accumulated
// M 3 -> value returned by the counter (which should add overall plus something done before)

using fmt::literals::operator""_format;

typedef struct disk_config {
    unsigned long long total_bytes_sec;
    unsigned long long read_bytes_sec;
    unsigned long long write_bytes_sec;
    unsigned long long total_iops_sec;
    unsigned long long read_iops_sec;
    unsigned long long write_iops_sec;
} _disk_config;

class DiskUtils
{
    // NOTE: The number of params (nparams) is 8, but that is because it later skips param 4 (errs).
    // If we want to use the i index in the domblkstats_output to iterate through it, we will go from 0 to 9 skipping 4.
    // For simplicity, it is easier to reseve 9 params

    unsigned long disk_stats[N_PARAMS][M];

    // Disk stats description
    // The 8 fields are:
    //   - 0:  n_read_ops;
    //   - 1:  n_read_bytes;
    //   - 2:  n_write_ops;
    //   - 3:  n_write_bytes;
    //   - 4:  errs (discarded by default by libvirt)
    //   - 5:  n_flush_ops;
    //   - 6:  duration_of_reads;
    //   - 7:  duration_of_writes;
    //   - 8:  duration_of_flushes;
    //
    // For each field, we keep 3 values
    //   - 0:  current value read
    //   - 1:  last value read before current (1-0 gives the value of the last quantum)
    //   - 2:  oveall read since the first read

    // The following ones determine the disk I/O limits to be set
    unsigned long long total_bytes_sec_limit;
    unsigned long long read_bytes_sec_limit;
    unsigned long long write_bytes_sec_limit;
    unsigned long long total_iops_sec_limit;
    unsigned long long read_iops_sec_limit;
    unsigned long long write_iops_sec_limit;

    // There are more options to control disk i/o throtling. For now, we consider the ones that seem more interesting
    // Available options: total_bytes_sec, read_bytes_sec, write_bytes_sec, total_iops_sec, read_iops_sec, write_iops_sec, total_bytes_sec_max, read_bytes_sec_max, write_bytes_sec_max,
    //          total_iops_sec_max, read_iops_sec_max, write_iops_sec_max, size_iops_sec, group_name, total_bytes_sec_max_length, read_bytes_sec_max_length, write_bytes_sec_max_length,
    //          total_iops_sec_max_length, read_iops_sec_max_length, write_iops_sec_max_length

    void update_stats(int i, unsigned long value);

  public:
    DiskUtils(disk_config dc)
    {
        for (int i = 0; i < N_PARAMS; i++) {
            for (int j = 0; j < M; j++) {
                disk_stats[i][j] = 0;
            }
        }
        total_bytes_sec_limit = dc.total_bytes_sec;
        read_bytes_sec_limit = dc.read_bytes_sec;
        write_bytes_sec_limit = dc.write_bytes_sec;
        total_iops_sec_limit = dc.total_iops_sec;
        read_iops_sec_limit = dc.read_iops_sec;
        write_iops_sec_limit = dc.write_iops_sec;
    };

    bool read_disk_stats(virDomainPtr dom);

    void print_disk_stats(const virDomainPtr dom, uint64_t interval_length_us,
                          uint32_t interval);
    void print_disk_stats_quantum(const virDomainPtr dom, int64_t delay);

    void apply_disk_util_limits(const virDomainPtr dom);

    void set_total_bytes_sec(
        const unsigned long long
            i); // total-bytes-sec - specifies total throughput limit in bytes per second
    void set_read_bytes_sec(
        const unsigned long long
            i); // read-bytes-sec - specifies read throughput limit in bytes per second
    void set_write_bytes_sec(
        const unsigned long long
            i); // write-bytes-sec - specifies write throughput limit in bytes per second
    void set_total_iops_sec(
        const unsigned long long
            i); // total-iops-sec - specifies total I/O operations limit per second
    void set_read_iops_sec(
        const unsigned long long
            i); // read-iops-sec - specifies read I/O operations limit per second
    void set_write_iops_sec(
        const unsigned long long
            i); // write-iops-sec - specifies write I/O operations limit per second

    double get_disk_bw_quantum(int64_t); // fast access to disk bw

    double get_disk_rd_bw_quantum(int64_t); // fast access to disk read bw

    double get_disk_wr_bw_quantum(int64_t); // fast access to disk write bw

    unsigned long long get_read_bytes_sec()
    {
        return disk_stats[1][3];
    };

    unsigned long long get_write_bytes_sec()
    {
        return disk_stats[3][3];
    };

    unsigned long long get_read_iops_sec()
    {
        return disk_stats[0][3];
    };

    unsigned long long get_write_iops_sec()
    {
        return disk_stats[2][3];
    };

    unsigned long long get_read_bytes_sec_q()
    {
        return disk_stats[1][0] - disk_stats[1][1];
    };

    unsigned long long get_write_bytes_sec_q()
    {
        return disk_stats[3][0] - disk_stats[3][1];
    };

    unsigned long long get_read_iops_sec_q()
    {
        return disk_stats[0][0] - disk_stats[0][1];
    };

    unsigned long long get_write_iops_sec_q()
    {
        return disk_stats[2][0] - disk_stats[2][1];
    };

    unsigned long long get_disk_io_time()
    {
        long read_time = disk_stats[6][0] - disk_stats[6][1];
        long write_time = disk_stats[7][0] - disk_stats[7][1];
        return read_time + write_time;
    };
};

/**
 * VIR_DOMAIN_BLOCK_STATS_FIELD_LENGTH:
 *
 * Macro providing the field length of parameter names when using
 * virDomainBlockStatsFlags().
 */
#define VIR_DOMAIN_BLOCK_STATS_FIELD_LENGTH VIR_TYPED_PARAM_FIELD_LENGTH

/**
 * VIR_DOMAIN_BLOCK_STATS_READ_BYTES:
 *
 * Macro represents the total number of read bytes of the
 * block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_READ_BYTES "rd_bytes"

/**
 * VIR_DOMAIN_BLOCK_STATS_READ_REQ:
 *
 * Macro represents the total read requests of the
 * block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_READ_REQ "rd_operations"

/**
 * VIR_DOMAIN_BLOCK_STATS_READ_TOTAL_TIMES:
 *
 * Macro represents the total time spend on cache reads in
 * nano-seconds of the block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_READ_TOTAL_TIMES "rd_total_times"

/**
 * VIR_DOMAIN_BLOCK_STATS_WRITE_BYTES:
 *
 * Macro represents the total number of write bytes of the
 * block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_WRITE_BYTES "wr_bytes"

/**
 * VIR_DOMAIN_BLOCK_STATS_WRITE_REQ:
 *
 * Macro represents the total write requests of the
 * block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_WRITE_REQ "wr_operations"

/**
 * VIR_DOMAIN_BLOCK_STATS_WRITE_TOTAL_TIMES:
 *
 * Macro represents the total time spend on cache writes in
 * nano-seconds of the block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_WRITE_TOTAL_TIMES "wr_total_times"

/**
 * VIR_DOMAIN_BLOCK_STATS_FLUSH_REQ:
 *
 * Macro represents the total flush requests of the
 * block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_FLUSH_REQ "flush_operations"

/**
 * VIR_DOMAIN_BLOCK_STATS_FLUSH_TOTAL_TIMES:
 *
 * Macro represents the total time spend on cache flushing in
 * nano-seconds of the block device, as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_FLUSH_TOTAL_TIMES "flush_total_times"

/**
 * VIR_DOMAIN_BLOCK_STATS_ERRS:
 *
 * In Xen this returns the mysterious 'oo_req', as an llong.
 */
#define VIR_DOMAIN_BLOCK_STATS_ERRS "errs"

#undef N_PARAMS
#undef M
