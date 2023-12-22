#!/bin/bash

# Runs the indicated benchmark and then shutdowns the VM

# Define folder path (inside VM)
# E.g., /home/VM_USER
FOLDER_PATH="XXXXX/XXXXX"

# Folder name where tailbench are found
TAIL_SOURCE="tailbench-repo/tailbench"

# Name of benchmark to be run
bench=$1

# Create run directory 
mkdir -p ${FOLDER_PATH}/run_dir

# Remove any old STARTED file
rm -rf ${FOLDER_PATH}/data_shared_folder/STARTED > /dev/null



### NAS benchmarks ###
if [[ $bench == "btCx" ]]; then
	${FOLDER_PATH}/NAS/bt.C.x
elif [[ $bench == "ftCx" ]]; then
	${FOLDER_PATH}/NAS/ft.C.x
elif [[ $bench == "cgCx" ]]; then
	${FOLDER_PATH}/NAS/cg.C.x
elif [[ $bench == "mgCx" ]]; then
	${FOLDER_PATH}/NAS/mg.C.x
elif [[ $bench == "isCx" ]]; then
	${FOLDER_PATH}/NAS/is.C.x

### Tailbench benchmark ###
elif [[ $bench == "img-dnn" ]]; then
    cd ${FOLDER_PATH}/${TAIL_SOURCE}/img-dnn/

    source ${FOLDER_PATH}/${TAIL_SOURCE}/configs.sh

    export TBENCH_SERVER_PORT=${2} # 9868
    export TBENCH_WARMUPREQS=${3}  # 5000
    export TBENCH_MAXREQS=${4}     # 10000
    THREADS=${5}                   # 1
    REQS=${6}                      # 100000000 # Set this very high; the harness controls maxreqs
    export TBENCH_NCLIENTS=${7}
    echo "TBENCH_NCLIENTS: "$TBENCH_NCLIENTS

    export TBENCH_START_FILE="${FOLDER_PATH}/data_shared_folder/STARTED"
    echo "TBENCH_START_FILE: " $TBENCH_START_FILE
    
	cpus="0"
	for i in $(seq 1 $(($THREADS-1))); do
		th=$((1*i))
		cpus="${cpus},$th"
	done
	echo "VCPUS: "$cpus

    TBENCH_SERVER_PORT=${TBENCH_SERVER_PORT} TBENCH_WARMUPREQS=${TBENCH_WARMUPREQS} TBENCH_MAXREQS=${TBENCH_MAXREQS} TBENCH_NCLIENTS=${TBENCH_NCLIENTS} taskset -c $cpus ${FOLDER_PATH}/${TAIL_SOURCE}/img-dnn/img-dnn_server_networked -r ${THREADS} -f ${FOLDER_PATH}/tailbench.inputs/img-dnn/models/model.xml -n ${REQS} &

    pid=$!
    sleep 5 # Wait for server to come up
    echo "Server img-dnn started. Clients can start now..."

    wait $pid

else
    echo "ERROR: Unknown benchmark."
    exit 1
fi

echo "${1} execution completed!"
rm -r ${FOLDER_PATH}/run_dir/

# We let the framework know that the server has completed by writting the SERVER_COMPLETED file in the shared folder
touch ${FOLDER_PATH}/data_shared_folder/SERVER_COMPLETED
