extern "C"
{
#include "libminiperf.h"
#include <unistd.h>
}
const char* input_name;

/*
 * Example usage: sudo ./libminiperf-test 2662 instructions,
 * where 2662 is the PID to be monitored
 * and instructions the event to be monitored
 * */

int main(int argc, char **argv)
{
	struct evlist* evlist = setup_events(argv[1], argv[2], "PID");
	enable_counters(evlist);

	while(true)
	{
		sleep(1);
		read_counters(evlist, NULL, NULL, NULL, NULL, NULL, NULL);
		print_counters(evlist);
	}

	return 0;
}
