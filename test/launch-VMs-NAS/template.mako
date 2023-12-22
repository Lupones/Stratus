<%include file="applications.mako"/>

tasks:
  - app: *cgCx_VM
    kind: VM
    ceph_vm: true
    domain_name: VM_Ceph_SMT_1
    snapshot_name: snap2
    ip: 192.168.10.101
    port: 9867
    arguments: ""
    cpus: [5,29,6,30,7,31]
  - app: *ftCx_VM
    kind: VM
    ceph_vm: true
    domain_name: VM_Ceph_SMT_2
    snapshot_name: snap2
    ip: 192.168.10.103
    port: 9867
    arguments: ""
    cpus: [0,24,1,25]
  - app: *mgCx_VM
    kind: VM
    ceph_vm: true
    domain_name: VM_Ceph_SMT_3
    snapshot_name: snap2
    ip: 192.168.10.105
    port: 9867
    arguments: ""
    cpus: [8,32,9,33,10,34]

cmd:
    ti: 1
    mi: 2000
    event: ["inst_retired.any:G,cpu_clk_unhalted.ref_tsc:G,cpu_clk_unhalted.thread:G,resource_stalls.sb:G,mem_load_retired.l1_miss:G,mem_load_retired.l2_miss:G,mem_load_retired.l3_miss:G,mem_load_retired.l3_hit:G,cycle_activity.stalls_total:G,cycle_activity.stalls_mem_any:G,resource_stalls.any:G"]

