# Stratus
Stratus Resource and Application Manager software developed at Universitat Politècnica de València (UPV).

This repository contains the code of the software developed for the experimental hardware testbed Stratus used to perform resource management research in cloud computing and high-performance computing (HPC). 

The devised framework has been designed to ease the management and control of the execution of one or more applications (running on VMs or bare-metal). Also, it allows monitoring and partitioning the main shared resources of the system (e.g., last level cache space, and memory, network and disk bandwisth) in order to carry out resource management research. 


For more information about Stratus please refer to the following papers:

L. Pons, S. Petit, J. Pons, M. E. Gómez, C. Huang and J. Sahuquillo, "Stratus: A Hardware/Software Infrastructure for Controlled Cloud Research," 2023 31st Euromicro International Conference on Parallel, Distributed and Network-Based Processing (PDP), Naples, Italy, 2023, pp. 299-306, doi: 10.1109/PDP59025.2023.00053.


### System Specifications

###### Hardware

Stratus' experimental framework is made up of three main nodes: server, client, and storage.

- The server node acts as the server side in our client-server architecture. This node runs the Stratus Resource and Application Manager software. This platform has an Intel(R) Xeon(R) Silver 4116 CPU processor with 12 Hyper-Threading cores (i.e., 24 logical cores). It supports Intel Resource Director Technologies (RDT), allowing to perform cache and memory bandwidth partitioning studies. The system is running an Ubuntu 18.04 with Linux kernel version 5.4 (5.4.0-56-generic). 
- The client node is an auxiliary node that emulates client behavior by executing client applications that perform requests to the VMs in the server node.
- The storage node provides remote storage resources for the VMs.

The server node is interconnected to the client and storage nodes with two distinct 20 Gbps networks.

###### Software

The current version of Stratus makes use of the following technologies.

**Technologies for cloud support**
-> Hypervisor: 		   KVM
-> Virtualizer: 	   QEMU
-> Virtualization Manager: Libvirt
-> Virtual switches: 	   OVS + DPDK
-> Remote storage: 	   Ceph 

**Techologies for resource monitoring and partitioning**
-> Hardware performance counters: Linux Perf (libminiperf library)
-> Energy consumption: 		  Intel Running Average Power Limit (RAPL)

-> CPU utilization: 	   VM's CPU utilization with libvirt and physical CPU utilization reading \proc\stat
-> Last Level Cache (LLC): Intel Cache Monitoring Technology (CMT) and Cache Allocation Technology (CAT)
-> Memory bandwidth: 	   Intel Memory Bandwidth Monitoring (MBM) and Memory Bandwidth Allocation (MBA)
-> Disk bandwidth: 	   libvirt 
-> Network bandwidth: 	   libvirt
 


### Installing and Building

After cloning the repository into your system and going into the corresponding folder, we need to download all the dependecies needed to run Stratus

1. The Linux kernel files are needed to make use of the methods to monitor the performance counters using Perf. In this way, we select those methods we need and pack them inside a simpler library name libminiperf.  

Download or link the Linux source files that match with the version of your linux kernel. For example, those for version 5.4 (5.4.0-56-generic) can be downloaded in the following link: https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.4.56.tar.gz.
```
$ tar -xvzf linux-5.4.56.tar.gz
$ mv linux-5.4.56 interference
$ cd Stratus
Stratus$ ln -s linux-5.4.56 linux
```

2. Since libminiperf/libminiperf.c has a main() method defined, before compiling perf, we need to rename int main(int argc, const char **argv) by int main_no_more(int argc, const char **argv) inside linux/tools/perf/perf.c. This is necessary since when including perf-in.o in libminiperf, if we leave the main() here, we will have an error saying main() is already defined.

3. Compile linux/tools/perf with the following command:

```
Stratus/linux/tools/perf$ V=1 NO_LIBPERL=1 NO_LIBPYTHON=1 NO_DWARF=1 NO_NEWT=1 NO_SLANG=1 NO_GTK2=1 NO_DEMANGLE=1 NO_LIBELF=1 NO_LIBUNWIND=1 NO_BACKTRACE=1 NO_LIBNUMA=1 NO_LIBAUDIT=1 NO_LIBBIONIC=1 NO_LIBCRYPTO=1 NO_LIBDW_DWARF_UNWIND=1 NO_PERF_READ_VDSO32=1 NO_PERF_READ_VDSOX32=1 NO_ZLIB=1 NO_LZMA=1 NO_AUXTRACE=1 NO_LIBBPF=1 NO_SDT=1 NO_JVMTI=1 HAVE_KVM_STAT_SUPPORT=0 DEBUG=0 CFLAGS=-O3 make
```

3. Download/clone (and build if needed) all the necessary libraries, each into a new folder:
- Linux Perf tool in the Linux source files - folder name *linux*
- [libcpuid: a small C library for x86 CPU detection and feature extraction](https://github.com/anrieff/libcpuid) - folder name *libcpuid*
- [FMT: a modern formatting library](https://github.com/fmtlib/fmt) - folder name *fmt*
- [cxx-prettyprint: a C++ library that allows automagic pretty-printing](https://github.com/louisdx/cxx-prettyprint) - folder name *cxx-prettyprint*
- [Intel(R) Resource Director Technology](https://github.com/intel/intel-cmt-cat) - folder name *intel-cmt-cat*
- [Intel Performance Counter Monitor](https://github.com/opcm/pcm) - folder name *intel-pcm*
- [stacktrace: C++ library for storing and printing backtraces](https://github.com/boostorg/stacktrace). Copy the contents of stacktrace/include/boost/ to folder named *boost*
```
Stratus$ cp -r stacktrace/include/boost/* boost 
```

**N.B. As libraries are changed and updated with time, further dependencies may appear.**


3. Build the framework: 

  a) Buid ~/Stratus/libminiperf library

  b) Build framework (Stratus).

```
Stratus$ cd libminiperf
Stratus/libminiperf$ make
Stratus/libminiperf$ cd ..
Stratus$ make
```

**N.B. Check that library paths in Makefile are correct for your user.**

## Repository Infrastructure

###### Launch experiment

- **scripts/launch.bash:** script that needs to be invoked to start running an experiment
- **scripts/templates/applications.mako:** applications to be used 

###### Running experiment

- **manager:** main class of Stratus 
- **common:** methods used to perform commons tasks such as set CPU affinity or read CPU statistics 
- **config:** class that is in charge of reading configuration file generated from template.mako and applying such configuration. It includes the available options to include in the template
- **log:** methods to print log messages using LOGINF interface
- **throw-with-trace:** methods to generate errors
- **policy:** define QoS policies. Test partitioning policy is defined as an example

###### Applications management

- **task:** definition of methods to be used on task (VMs or applications)
   - **app-task:** methods specific of tasks that are applications running natively on the machines (HPC) 
   - **vm-task:** methods specific to task that are application running on VMs (cloud)

###### Resources monitoring and partitioning. Statistics.

- **disk-utils:** methods to read and partition disk BW
- **events-perf:** methods to setup and read performance counters
- **intel-rdt:** methods to read and partition LLC space and memory bandwidth
- **net-bandwidth:** methods to read and partition network BW
- **stats:** methods to generate statistics based on data collected using the above classes


## Running the tests

### Experiments with VMs and client-server applications

The framework prepares, launches and monitors the VMs indicated in the experiment configuration file (i.e., template.mako). 
A yaml configuration file is generated based on this MAKO template, which includes all the necessary data to run both the VM and the application that is going to be executed.

The framework starts the server VM by sending through **ssh** a command to launch the script: **run_script_server_2.sh**. An example of this script is found inside the *client_server_scripts* folder. This script parses the application (i.e. task) arguments, which have to be included in the template.mako file, and creates a temporary directory (e.g. **/home/dsf_VM_NAME/**) that stores the inputs and outputs of the running experiment.

The framework starts the client (native execution) by sending through **ssh** a command to launch the script **run_script_client_native_2.sh** (also found inside the *client_server_scripts* folder). This script parses the client arguments, which have to be included in the template.mako file, and creates a temporal directory in the client machine (e.g. **/home/dsf_VM_NAME/**) that stores the inputs and outputs of the client.

The source code that performs the ssh operations and launches both server and client is located at **vm-task.cpp**.

**N.B. All the code should be installed only in the main node. No code is needed in the client node.** 


### Examples provided

A **test** folder has been created. It includes several directories -lauch-VM-tailbench, launch-graph-LLCpart, launch-VMs-NAS - that correspond to different experiments.

- **lauch-VM-tailbench:** Experiment launching Tailbench application img-dnn in a single VM. Tailbench applications can be downloaded at https://github.com/supreethkurpad/Tailbench
- **launch-graph-LLCpart:** Experiment launching graph applications from GAP benchmark suite (algorithm PR + inputs Kron, Twitter, Urand and Web). The template specifies that the LLC space of the application launched is reduced to 7 cache ways. GAP benchmarks can be downloaded at https://github.com/sbeamer/gapbs
- **launch-VMs-NAS:** Experiment launching three NAS Parallel applications (cg.C.x, ft.C.x and mg.C.x) in three different VMs. NAS Parallel applications can be downloaded at https://www.nas.nasa.gov/software/npb.html

Each folder includes a file, **template.mako**, that indicates the policy to launch as well as other execution parameters such as the performance counters that are going to be monitored. 

A **apps.yaml** file is also needed. This file specifies the workload mixes to launch. Each line represents a mix, and each application in the mix is separated by a coma. The file **Stratus/scripts/templates/applications.mako** specifies each of the applications that can be used. **IMPORTANT NOTE: The root of the applications needs to be modified to adapt to your file path.**

To launch a given experiment, execute the following command
```
Stratus/test/FOLDER# bash Stratus/scripts/launch.bash apps.yaml
```

By default, experiments are launched 3 times. In **scripts/launch.bash** you can find the possible parameters that can be speficied. 


## IMPORTANT CONSIDERATIONS TO USE STRATUS

Below, we indicate some important issues that should be taken into account:

1. **Dependencies and installation:** As explained above, the current version of Stratus is installed in an Intel(R) Xeon(R) Silver 4116 CPU @ 2.10GHz with a 2-socket 12 SMT-2 cores, running an Ubuntu 18.04 with Linux kernel version 5.4 (5.4.0-56-generic). Installing Stratus in a different system or using other hardware may cause the framework to fail, since tools like Perf are very dependant on the underlying system.

2. **libvirt and Ceph:** Stratus takes advantage from libvirt and Ceph technologies to manage the VMs and store the images. Some of the commands coded in the framework directly interact with these tools, and therefore they may fail if the configuration is not similar. For instance, current Ceph installation takes "libvirt" as user, and "libvirt-pool" as the name of the OSD pool. If your Ceph installation includes several pools or users, the framework needs to be adapted.

3. **Network:** network was configured with an OVS+DPDK setting. Stratus assumes this configuration and makes other assumptions such as the name of the interfaces of the virtual switch (dpdk0 and dpdk1) and of the interfaces of the VMs (the naming convention followed is vhost-VM_DOMAIN_NAME). **These names should be adapted in net-bandwidth.cpp** to reflect the actual names in the experimental platform if they are different. 

4. **Adjustment of paths and script locations:** As indicated above, the framework launches the VMs and sets up the server and client sides using several bash scripts. The location of these scripts, and the structure of directories expected by the framework, is not dynamically obtained. Scripts are launched by ssh commands that correspond to static strings, as can be seen, for instance, in the code of vm-task.cpp. Should these paths or scripts be different, the framework needs to be adapted.

5. **Tailbench instrumentation:** In order to perform an accurate study, the applications run in the environment were instrumented, for instance recording when transmissions and receptions effectively begin. The framework therefore expects that applications write an empty file named STARTED when they begin transmitting and answering requests. If non-instrumented applications were to be used, the user must be aware that final results regarding performance estimation may differ, since initialization quantums (which are currently discarded) would be counted.

## Contributors

```
[Lucia Pons](https://github.com/Lupones) <lupones@disca.upv.es>
[Josué Feliu](https://github.com/jofepre) <jofepre@gap.upv.es>
[Jose Puche](https://github.com/JPuche)
[Carlos Navarro](https://github.com/cnsval)
[Vicent Selfa](https://github.com/vtselfa)
```

## License

This project is licensed under the Apache License, Version 2.0. - see the LICENSE file for details.

## How to Cite

Please cite Stratus by the following publications:

L. Pons, S. Petit, J. Pons, M. E. Gómez, C. Huang and J. Sahuquillo, "Stratus: A Hardware/Software Infrastructure for Controlled Cloud Research," 2023 31st Euromicro International Conference on Parallel, Distributed and Network-Based Processing (PDP), Naples, Italy, 2023, pp. 299-306, doi: 10.1109/PDP59025.2023.00053.
