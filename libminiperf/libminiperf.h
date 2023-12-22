#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define METRIC_ONLY_LEN 20

struct evlist;


void read_counters(struct evlist *evsel_list, const char **names, double *results, const char **units, bool *snapshot, uint64_t *enabled, uint64_t *running);
void get_names(struct evlist *evsel_list, const char **names);
void enable_counters(struct evlist *evsel_list);
void disable_counters(struct evlist *evsel_list);
struct evlist* setup_events(const char *monitor_target, const char *events, const char *type);
void print_counters(struct evlist *evsel_list);
void clean(struct evlist *evsel_list);
int num_entries(struct evlist *evsel_list);
