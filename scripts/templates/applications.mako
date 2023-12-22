applications:
  gapbs:
    prRoad: &prRoad
      name: prRoad
      client: false
      cmd: gapbs/pr -f gapbs/benchmark/graphs/road.sg -i1000 -t1e-4 -n16

    prKron: &prKron
      name: prKron
      client: false
      cmd: gapbs/pr -f gapbs/benchmark/graphs/kron.sg -i1000 -t1e-4 -n16

    prTwitter: &prTwitter
      name: prTwitter
      client: false
      cmd: gapbs/pr -f gapbs/benchmark/graphs/twitter.sg -i1000 -t1e-4 -n16

    prUrand: &prUrand
      name: prUrand
      client: false
      cmd: /vmssd/benchmarks/gapbs/pr -f gapbs/benchmark/graphs/urand.sg -i1000 -t1e-4 -n16

    prWeb: &prWeb
      name: prWeb
      client: false
      cmd: gapbs/pr -f gapbs/benchmark/graphs/web.sg -i1000 -t1e-4 -n16

  VMs:
    ftCx_VM: &ftCx_VM
      name: ftCx_VM
      client: false
      args: ftCx

    cgCx_VM: &cgCx_VM
      name: cgCx_VM
      client: false
      args: cgCx

    mgCx_VM: &mgCx_VM
      name: mgCx_VM
      client: false
      args: mgCx

    img-dnn_VM: &img-dnn_VM
      name: img-dnn_VM
      args: img-dnn _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: img-dnn _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_

    masstree_VM: &masstree_VM
      name: masstree_VM
      args: masstree _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: masstree _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_

    silo_VM: &silo_VM
      name: silo_VM
      args: silo _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: silo _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_

    moses_VM: &moses_VM
      name: moses_VM
      args: moses _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: moses _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_

    shore_VM: &shore_VM
      name: shore_VM
      args: shore _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: shore _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_

    sphinx_VM: &sphinx_VM
      name: sphinx_VM
      args: sphinx _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: sphinx _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_

    specjbb_VM: &specjbb_VM
      name: specjbb_VM
      args: specjbb _TBENCH_SERVER_PORT_ _TBENCH_ARGS_
      client: yes
      client_args: specjbb _TBENCH_SERVER_ _TBENCH_SERVER_PORT_ _TBENCH_CLIENT_ARGS_


