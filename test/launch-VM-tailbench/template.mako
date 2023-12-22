<%include file="applications.mako"/>


tasks:
  - app: *img-dnn_VM
    kind: VM
    ceph_vm: true
    client_native: true
    domain_name: VM_Ceph_SMT_1
    snapshot_name: snap2
    ip: 192.168.10.101
    port: 9867
    arguments: 2000 100000 2 100000000000 12
    client_arguments: 16 100000 1 0 0 0 0 12
    cpus: [6,30]



cmd:
    ti: 1
    mi: 1000
    cpu-affinity: [5]
    event: ["inst_retired.any:G,cpu_clk_unhalted.ref_tsc:G,cpu_clk_unhalted.thread:G,resource_stalls.sb:G,mem_load_retired.l1_miss:G,mem_load_retired.l2_miss:G,mem_load_retired.l3_miss:G,cycle_activity.stalls_total:G,cycle_activity.stalls_mem_any:G,resource_stalls.any:G,int_misc.clear_resteer_cycles:G,br_misp_retired.all_branches:G"]

