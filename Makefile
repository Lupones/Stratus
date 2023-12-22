CPPFLAGS += -I. -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I/usr/lib/glib-2.0/include -Ifmt -Ilibminiperf -Ilibcpuid/libcpuid/
CPPFLAGS += -Lintel-cmt-cat/lib -Lintel-pcm -Lfmt/fmt -Llibminiperf -Llibcpuid/libcpuid/.libs/
CPPFLAGS += -DBOOST_LOG_DYN_LINK -DBOOST_STACKTRACE_USE_BACKTRACE -DHAVE_STDINT_H

CXXFLAGS += -Wall -Wshadow -g -O0 -std=c++14

LIBS = -lpthread -lrt -lboost_system -lboost_log -lboost_log_setup -lboost_thread -lboost_filesystem -lyaml-cpp -lpqos -lboost_program_options -lglib-2.0 -lPCM -lfmt -lminiperf -ldl -lbacktrace -lm -lbfd -l:libcpuid.a -lz -lvirt -lpython2.7 -llzma

SRCS = intel-rdt.cpp policy.cpp common.cpp config.cpp events-perf.cpp log.cpp manager.cpp stats.cpp vm-task.cpp net-bandwidth.cpp disk-utils.cpp task.cpp app-task.cpp

manager: $(SRCS:.cpp=.o) libminiperf/libminiperf.a
	make -C intel-pcm
	make -C intel-cmt-cat SHARED=0
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)


clean:
	rm -rf *.o manager


distclean: clean
	make -C intel-pcm/lib clean
	make -C intel-cmt-cat clean

include rules.mk

.PHONY: clean distclean


