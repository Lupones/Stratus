#include "events-perf.hpp"
#include <unistd.h>

int main(int argc, char **argv)
{
	auto perf = Perf();
	pid_t pid = std::stoi(argv[1]);
	perf.init();
	perf.setup_events(pid, {argv[2]});

	while(true)
	{
		sleep(1);
		perf.read_groups(pid);
		perf.print_counts(pid);
		break;
	}

	return 0;
}
