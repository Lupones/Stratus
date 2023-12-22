<%include file="applications.mako"/>

clos:
  - num: 1
    schemata: 0x7f

tasks:
  % for app in apps:
  - app: *${app}
    kind: app
    single-pid: true
    initial_clos: 1
    cpus: [0,24,1,25,2,26,3,27,4,28,5,29,6,30,7,31,8,32,9,33,10,34,11,35]
  % endfor

cmd:
    ti: 0.5
    mi: 5000
    event: ["inst_retired.any,cpu_clk_unhalted.ref_tsc,mem_load_retired.l3_miss,mem_load_retired.l3_hit,mem_load_retired.l2_miss,cycle_activity.cycles_l2_miss,l2_trans.l2_wb,idi_misc.wb_upgrade,idi_misc.wb_downgrade,l2_rqsts.miss,offcore_response.demand_data_rd.l3_miss.any_snoop,offcore_response.all_pf_data_rd.l3_miss.any_snoop,offcore_response.demand_data_rd.l3_hit.any_snoop,offcore_response.all_pf_data_rd.l3_hit.any_snoop,l2_lines_in.all,l2_lines_out.non_silent,l2_lines_out.silent,l2_lines_out.useless_hwpf,l2_rqsts.demand_data_rd_miss,offcore_response.pf_l1d_and_sw.l3_miss.any_snoop,offcore_response.pf_l1d_and_sw.any_response,cycle_activity.stalls_l2_miss,cycle_activity.stalls_mem_any,cycle_activity.stalls_total,mem_load_retired.l1_miss,mem_load_retired.l1_hit,mem_load_retired.l2_hit,l2_rqsts.references"]
