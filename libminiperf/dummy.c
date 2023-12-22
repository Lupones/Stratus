#include <stdbool.h>
#include <unistd.h>

struct scripting_ops;
//int script_spec_register(const char *spec, struct scripting_ops *ops) {return 0;}

bool test_attr__enabled = false;
struct perf_event_attr;
//void test_attr__open(struct perf_event_attr *attr, pid_t pid, int cpu, int fd, int group_fd, unsigned long flags){}

struct perf_evsel;
struct perf_sample;
struct event_key;
struct perf_kvm_stat *kvm;
//bool exit_event_begin(struct perf_evsel *evsel, struct perf_sample *sample, struct event_key *key) {return false;}
//bool exit_event_end(struct perf_evsel *evsel, struct perf_sample *sample, struct event_key *key) {return false;}
//bool kvm_entry_event(struct perf_evsel *evsel) {return false;}
//bool kvm_exit_event(struct perf_evsel *evsel) {return false;}
//void exit_event_decode_key(struct perf_kvm_stat *kvm, struct event_key *key, char *decode) {}
int verbose = 0;
