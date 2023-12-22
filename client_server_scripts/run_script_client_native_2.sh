#!/bin/bash

####################################################
################ I M P O R T A N T #################
####################################################
## This script must be located in the CLIENT node ##


# Just in case... 
sleep 5

# Define folder path (inside VM)
# E.g., /home/VM_USER
FOLDER_PATH="XXXXX/XXXXX"

# Folder name where tailbench are found
TAIL_SOURCE="/home/tailbench-repo/tailbench"
TAIL_DATA="/home/tailbench_data/tailbench.inputs"

CPU_INI=2
cpu_num=$CPU_INI

user=${1}
domain_name=${2}
run_id=${3}
bench=${4}

export TBENCH_SERVER=${5}
export TBENCH_SERVER_PORT=${6}
export TBENCH_QPS=${7}  
export TBENCH_MINSLEEPNS=${8}
export TBENCH_CLIENT_THREADS=${9}
export TBENCH_VARQPS=${10}
export TBENCH_INIQPS=${11}
export TBENCH_INTERVALQPS=${12}
export TBENCH_STEPQPS=${13}
export TBENCH_NCLIENTS=${14}

# Temporarily using the app name in the shared folder
data_shared_folder="/home/dsf_${domain_name}"

run_dir="/home/${user}/run_dir_${domain_name}"
echo $run_dir
mkdir -p ${run_dir}
cd ${run_dir}

rm -rf ${TAIL_SOURCE}/${bench}/client_* &> /dev/null
rm -rf ${TAIL_SOURCE}/${bench}/lats_* &> /dev/null

cpus="0"
for i in $(seq 1 $(($TBENCH_CLIENT_THREADS-1))); do
    cpus="${cpus},$i"
done
#echo "VCPUS: "$cpus
echo "Number of clients: "$TBENCH_NCLIENTS

# xapian
    
if [[ $bench == "img-dnn" ]]; then

    source ${TAIL_SOURCE}/configs.sh

    #default parameters:  TBENCH_QPS=500
    #export REQS=100000000 # Set this very high; the harness controls maxreqs

	for i in $(seq 0 $(($TBENCH_NCLIENTS-1))); do
        th0=$cpu_num
        th1=$((th0 + 12))
		echo "Client ID: "$i", taskset -c "$th0" "$th1
        echo "Client launched"
        date
		TBENCH_ID=${i} TBENCH_SERVER=${TBENCH_SERVER} TBENCH_SERVER_PORT=${TBENCH_SERVER_PORT} TBENCH_VARQPS=${TBENCH_VARQPS} TBENCH_MINSLEEPNS=${TBENCH_MINSLEEPNS} TBENCH_INIQPS=${TBENCH_INIQPS} TBENCH_INTERVALQPS=${TBENCH_INTERVALQPS} TBENCH_STEPQPS=${TBENCH_STEPQPS}  TBENCH_CLIENT_THREADS=${TBENCH_CLIENT_THREADS} TBENCH_QPS=${TBENCH_QPS} TBENCH_MNIST_DIR=${TAIL_DATA}/img-dnn/mnist taskset -c $th0,$th1 ${TAIL_SOURCE}/img-dnn/img-dnn_client_networked &> ${data_shared_folder}/client_output_${run_id}_${i}.txt &
		echo $! > client_${i}.pid

        #cpu_num=$(((cpu_num+1)))
		cpu_num=$(((cpu_num+1)%12))
		if [ $cpu_num -eq 12 ]; then
            cpu_num=$CPU_INI
        fi
    done
    
    #Create this file in dsf to let the manager know that it can start printing stats
    echo "CLIENT_LAUNCHED" > ${data_shared_folder}/CLIENT_LAUNCHED
    echo "Cient img-dnn started..."

    for i in $(seq 0 $(($TBENCH_NCLIENTS-1))); do
        wait $(cat client_${i}.pid)
    done

    echo "The client has completed its execution"

    
else
    echo "ERROR: Unknown benchmark."
    exit 1
fi


# Format data of Tailbench
if [[ $bench == "img-dnn" ]]; then
    echo "I am "$bench
    for i in $(seq 0 $(($TBENCH_NCLIENTS-1))); do
        echo "Client: "$i

        if [ -e ${run_dir}/lats_$i.bin ]; then
            ${TAIL_SOURCE}/utilities/parselats.py ${run_dir}/lats_$i.bin > ${run_dir}/latsEcho_${run_id}_$i.txt
            mv ${run_dir}/lats.txt ${run_dir}/lats_${run_id}_$i.txt

            rm -rf ${run_dir}/lats_$i.bin
        fi
        
    	rm -rf ${run_dir}/client_$i.pid
    done
fi

# Copy data to the shared folder
cp ${run_dir}/* ${data_shared_folder}/


echo "${1} execution completed!"
rm -r ${run_dir}/


