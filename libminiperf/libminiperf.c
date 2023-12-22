#include <linux/time64.h>

#include "util/stat.h"
#include "util/thread_map.h"
#include "util/target.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/counts.h"
#include "util/parse-events.h"
#include "util/cpumap.h"


#include "libminiperf.h"

struct target target = {
	.uid = UINT_MAX
};


struct perf_stat_config stat_config = {
  	.aggr_mode      = AGGR_GLOBAL,
    .scale          = true,
    //.unit_width     = 4, /* strlen("unit") */
    //.run_count      = 1,
//    .metric_only_len    = METRIC_ONLY_LEN,
//    .walltime_nsecs_stats   = &walltime_nsecs_stats,
//    .big_num        = true,
};


static inline void diff_timespec(struct timespec *r, struct timespec *a,
				 struct timespec *b)
{
	r->tv_sec = a->tv_sec - b->tv_sec;
	if (a->tv_nsec < b->tv_nsec)
	{
		r->tv_nsec = a->tv_nsec + NSEC_PER_SEC - b->tv_nsec;
		r->tv_sec--;
	}
	else
	{
		r->tv_nsec = a->tv_nsec - b->tv_nsec ;
	}
}

/*
 * Read out the results of a single counter
 */
static int read_counter(struct evlist *evsel_list, struct evsel *counter)
{
	int nthreads = perf_thread_map__nr(evsel_list->core.threads);
	int ncpus, cpu, thread;

	if (target__has_cpu(&target) && !target__has_per_thread(&target))
		ncpus = perf_evsel__nr_cpus(counter);
	else
		ncpus = 1;

	if (!counter->supported)
		return -ENOENT;

	for (thread = 0; thread < nthreads; thread++) {
		for (cpu = 0; cpu < ncpus; cpu++) {
			//struct perf_counts_values *count;
            //count = perf_counts(counter->counts, cpu, thread);

			if (!perf_counts__is_loaded(counter->counts, cpu, thread) && perf_evsel__read_counter(counter, cpu, thread)) {
				counter->counts->scaled = -1;
				perf_counts(counter->counts, cpu, thread)->ena = 0;
                perf_counts(counter->counts, cpu, thread)->run = 0;
				return -1;
			}

			perf_counts__set_loaded(counter->counts, cpu, thread, false);
			/*if (perf_evsel__write_stat_event(counter, cpu, thread, count)) {
                pr_err("failed to write stat event\n");
                return -1;
            }*/

		}
	}

	return 0;
}


void read_counters(struct evlist *evsel_list, const char **names, double *results, const char **units, bool *snapshot, uint64_t *enabled, uint64_t *running)
{
	struct evsel *counter;
	int ret;
	/*struct perf_stat_config stat_config =
	{
		.aggr_mode	= AGGR_GLOBAL,
		.scale		= true,
	};*/

	evlist__for_each_entry(evsel_list, counter)
	{
		ret = read_counter(evsel_list, counter);
        if (ret)
            pr_debug("failed to read counter %s\n", counter->name);

        if (ret == 0 && perf_stat_process_counter(&stat_config, counter))
            pr_warning("failed to process counter %s\n", counter->name);
	}

	size_t i = 0;
	evlist__for_each_entry(evsel_list, counter)
	{
		int nthreads = perf_thread_map__nr(counter->core.threads);
		int ncpus, cpu, thread;

		if (target__has_cpu(&target) && !target__has_per_thread(&target))
			ncpus = perf_evsel__nr_cpus(counter);
		else
			ncpus = 1;

		uint64_t ena = 0, run = 0, val = 0;
		for (thread = 0; thread < nthreads; thread++)
		{
			for (cpu = 0; cpu < ncpus; cpu++)
			{
				val += perf_counts(counter->counts, cpu, thread)->val;
				ena += perf_counts(counter->counts, cpu, thread)->ena;
				run += perf_counts(counter->counts, cpu, thread)->run;
			}
			assert(run <= ena);
		}
		if (names)
			names[i] = counter->name;
		if (results)
			results[i] = val * counter->scale;
		if (units)
			units[i] = counter->unit;
		if (snapshot)
			snapshot[i] = counter->snapshot;
			//snapshot[i] = true;
		if (enabled)
			enabled[i] = ena;
		if (running)
			running[i] = run;
		i++;
	}
}


void get_names(struct evlist *evsel_list, const char **names)
{
	struct evsel *counter;

	size_t i = 0;
	evlist__for_each_entry(evsel_list, counter)
	{
		if (names)
			names[i] = counter->name;
		i++;
	}
}


void enable_counters(struct evlist *evsel_list)
{
	/*
	 * We need to enable counters only if:
	 * - we don't have tracee (attaching to task or cpu)
	 * - we have initial delay configured
	 */
	evlist__enable(evsel_list);
}


void disable_counters(struct evlist *evsel_list)
{
	/*
	 * If we don't have tracee (attaching to task or cpu), counters may
	 * still be running. To get accurate group ratios, we must stop groups
	 * from counting before reading their constituent counters.
	 */
	evlist__disable(evsel_list);
}


void print_counters2(struct evlist *evsel_list, struct timespec *ts)
{
	struct evsel *counter;

	evlist__for_each_entry(evsel_list, counter)
	{
		struct perf_stat_evsel *ps = counter->priv;
		double avg = avg_stats(&ps->res_stats[0]);
		double uval;
		double ena, run;

		ena = avg_stats(&ps->res_stats[1]);
		run = avg_stats(&ps->res_stats[2]);

		uval = avg * counter->scale;
		fprintf(stdout, "%f %s %s", uval, counter->unit, counter->name);
		if (run != ena)
			fprintf(stdout, "  (%.2f%%)", 100.0 * run / ena);
		fprintf(stdout, "\n");
	}

	fflush(stdout);
}

static int perf_stat_init_aggr_mode(struct evlist *evsel_list)
{
    int nr;

    switch (stat_config.aggr_mode) {
    /*case AGGR_SOCKET:
        if (cpu_map__build_socket_map(evsel_list->core.cpus, &stat_config.aggr_map)) {
            perror("cannot build socket map");
            return -1;
        }
        stat_config.aggr_get_id = perf_stat__get_socket_cached;
        break;
    case AGGR_DIE:
        if (cpu_map__build_die_map(evsel_list->core.cpus, &stat_config.aggr_map)) {
            perror("cannot build die map");
            return -1;
        }
        stat_config.aggr_get_id = perf_stat__get_die_cached;
        break;
    case AGGR_CORE:
        if (cpu_map__build_core_map(evsel_list->core.cpus, &stat_config.aggr_map)) {
            perror("cannot build core map");
            return -1;
        }
        stat_config.aggr_get_id = perf_stat__get_core_cached;
        break;
    case AGGR_NONE:
        if (term_percore_set()) {
            if (cpu_map__build_core_map(evsel_list->core.cpus,
                            &stat_config.aggr_map)) {
                perror("cannot build core map");
                return -1;
            }
            stat_config.aggr_get_id = perf_stat__get_core_cached;
        }
        break;*/
    case AGGR_GLOBAL:
    case AGGR_THREAD:
    case AGGR_UNSET:
    default:
        break;
    }

    /*
     * The evsel_list->cpus is the base we operate on,
     * taking the highest cpu number to be the size of
     * the aggregation translate cpumap.
     */
    nr = perf_cpu_map__max(evsel_list->core.cpus);
    stat_config.cpus_aggr_map = perf_cpu_map__empty_new(nr + 1);
    return stat_config.cpus_aggr_map ? 0 : -ENOMEM;
}



struct evlist* setup_events(const char *monitor_target, const char *events, const char *type)
{
	/*int interval = stat_config.interval;
    int times = stat_config.times;
    int timeout = stat_config.timeout;*/
	struct evlist	*evsel_list = NULL;
	bool group = false;
	//char msg[BUFSIZ];

	// Assign PID or CPU depending on type
	if (strcmp(type, "PID") == 0)
		target.pid = monitor_target;
	else if (strcmp(type, "CPU") == 0)
		target.cpu_list = monitor_target;

	evsel_list = evlist__new();
	if (evsel_list == NULL) {
		pr_err("evsel_list is NULL");
		return NULL;
	}

	//perf_stat__collect_metric_expr(evsel_list);
	//perf_stat__init_shadow_stats();


	if (parse_events(evsel_list, events, NULL)) {
		goto out;
	}


	target__validate(&target);

	if ((stat_config.aggr_mode == AGGR_THREAD) && (target.system_wide))
        target.per_thread = true;

	if (perf_evlist__create_maps(evsel_list, &target) < 0) {
		if(target__has_task(&target))
			pr_err("Problems finding threads of monitor\n");
		else if (target__has_cpu(&target))
			pr_err("Problems finding CPUs of monitor\n");
		goto out;
	}
	perf_cpu_map__put(evsel_list->core.cpus);

	if (strcmp(type, "PID") == 0) {
        thread_map__read_comms(evsel_list->core.threads);
    }
	//perf_thread_map__put(evsel_list->core.threads);

	if (perf_evlist__alloc_stats(evsel_list, true))
		goto out;

	if (perf_stat_init_aggr_mode(evsel_list))
        goto out;

	if (group)
		perf_evlist__set_leader(evsel_list);


	struct evsel *counter;
	evlist__for_each_entry(evsel_list, counter) {
		if (create_perf_stat_counter(counter, &stat_config, &target) < 0)
			exit(-1);
		counter->supported = true;
	}

	if (perf_evlist__apply_filters(evsel_list, &counter)) {
		pr_err("failed to set filter \"%s\" on event %s with %d\n", counter->filter, perf_evsel__name(counter), errno);
		exit(-1);
	}

	/*struct perf_evsel_config_term *err_term;
	if (perf_evlist__apply_drv_configs(evsel_list, &counter, &err_term))
	{
		pr_err("failed to set config \"%s\" on event %s with %d (%s)\n",
		      err_term->val.drv_cfg, perf_evsel__name(counter), errno, strerror(errno));
		exit(-1);
	}*/

	return evsel_list;
out:
	evlist__delete(evsel_list);
	return NULL;
}


void print_counters(struct evlist *evsel_list)
{
	struct evsel *counter;
	evlist__for_each_entry(evsel_list, counter)
	{
		int nthreads = perf_thread_map__nr(counter->core.threads);
		int ncpus, cpu, thread;

		if (target__has_cpu(&target) && !target__has_per_thread(&target))
			ncpus = perf_evsel__nr_cpus(counter);
		else
			ncpus = 1;

		uint64_t ena = 0, run = 0, val = 0;
		double uval;

		for (thread = 0; thread < nthreads; thread++)
		{
			for (cpu = 0; cpu < ncpus; cpu++)
			{
				val += perf_counts(counter->counts, cpu, thread)->val;
				ena += perf_counts(counter->counts, cpu, thread)->ena;
				run += perf_counts(counter->counts, cpu, thread)->run;
			}
		}
		uval = val * counter->scale;
		fprintf(stdout, "%f %s %s", uval, counter->unit, counter->name);
		if (run != ena)
			fprintf(stdout, "  (%.2f%%)", 100.0 * run / ena);
		fprintf(stdout, "\n");
	}
}


void clean(struct evlist *evsel_list)
{
	/*
	 * Closing a group leader splits the group, and as we only disable
	 * group leaders, results in remaining events becoming enabled. To
	 * avoid arbitrary skew, we must read all counters before closing any
	 * group leaders.
	 */
	disable_counters(evsel_list);
	read_counters(evsel_list, NULL, NULL, NULL, NULL, NULL, NULL);
	evlist__close(evsel_list);
	perf_evlist__free_stats(evsel_list);
	evlist__delete(evsel_list);
}


int num_entries(struct evlist *evsel_list)
{
	return evsel_list->core.nr_entries;
}
