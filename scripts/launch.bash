
#!/bin/bash

# Sync date
ntpdate -u 0.europe.pool.ntp.org

#Save the actual user
_USER=$(who am i | awk '{print $1}')

# Dictionary with VM-app pairs
declare -A arr

function join_by { local IFS="$1"; shift; echo "$*"; }

getopt --test > /dev/null
if [[ $? -ne 4 ]]; then
    echo "I’m sorry, `getopt --test` failed in this environment."
    exit 1
fi

#OPTIONS=
LONGOPTIONS=max-rep:,ini-rep:,clog-min:,clients:,spec,monitor-only

# -temporarily store output to be able to check for errors
# -e.g. use “--options” parameter by name to activate quoting/enhanced mode
# -pass arguments only via   -- "$@"   to separate them correctly
PARSED=$(getopt --options=$OPTIONS --longoptions=$LONGOPTIONS --name "$0" -- "$@")
if [[ $? -ne 0 ]]; then
    # e.g. $? == 1
    #  then getopt has complained about wrong arguments to stdout
    exit 2
fi
# read getopt’s output this way to handle the quoting right:
eval set -- "$PARSED"

# now enjoy the options in order and nicely split until we see --
INI_REP=0
MAX_REP=3
CLOG_MIN=inf
CLIENT_NUM=1
SPEC=false
MONITOR=false
while true; do
    case "$1" in
        --max-rep)
            MAX_REP="$2"
            shift 2
            ;;
        --ini-rep)
            INI_REP="$2"
            shift 2
            ;;
        --clog-min)
            CLOG_MIN="$2"
            shift 2
            ;;
		--clients)
            CLIENT_NUM="$2"
            shift 2
            ;;
		--spec)
			SPEC=true
			shift 1
			;;
		--monitor-only)
			MONITOR=true
			shift 1
			;;
        --)
            shift
            break
            ;;
        *)
            echo "Programming error"
			echo $@
            exit 3
            ;;
    esac
done

# handle non-option arguments
if [[ $# -ne 1 ]]; then
    echo "$0: A single input file is required."
	echo $@
    exit 4
fi

WORKLOADS=$1

DIR=$(dirname $0)

source ${DIR}/config.bash

if ! $MONITOR; then
	#Clean the client data_shred_folder
	echo "Cleaning shared folders..."
	echo "ssh -p 3322 ${_USER}@xpl2.gap.upv.es 'rm -r /home/data_shared_folder/* &>/dev/null'"
	ssh -p 3322 ${_USER}@xpl2.gap.upv.es 'rm -r /home/data_shared_folder/* &>/dev/null'
	echo "rm -r /homenvm/data_shared_folder/*"
	rm -r /homenvm/data_shared_folder/*
fi

# Get list of VMs to run
VMs=$(cat template.mako | grep "domain_name:" | awk '{printf("%s ", $2)}')
APPs=$(cat template.mako | grep "app:" | awk '{printf("%s ", $3)}')
APPs=$(echo $APPs  | tr '*' " ")
appNum=0
for VM in $(echo $VMs); do
	if ! $MONITOR; then
    	echo "ssh -p 3322 ${_USER}@xpl2.gap.upv.es 'rm -r /home/dsf_${VM}/* &>/dev/null'"
    	ssh -p 3322 ${_USER}@xpl2.gap.upv.es "rm -r /home/dsf_${VM}/* &>/dev/null"
    	rm -rf /homenvm/dsf_${VM}/*
	fi

	# Add VM-app pairs to dicitionary
	count=0
	for app in $APPs; do
		if [ "$appNum" -eq "$count" ];then
			arr[$VM]=$app
		fi
		count=$((count+1))
	done
	appNum=$((appNum+1))
done

# Clean CACHE
sync
echo "echo 1 > /proc/sys/vm/drop_caches"

# Enable prefetchers
wrmsr -a 0x1A4 0x0

# Enable CAT and MBA
umount /sys/fs/resctrl
mount -t resctrl resctrl -o cdp,mba_MBps /sys/fs/resctrl

# Set performance governor
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null
echo 2100000 | tee /sys/devices/system/cpu/cpufreq/policy*/scaling_min_freq > /dev/null

# Save manager binary
if ! $MONITOR; then
	cp ${HOME}/interference/manager .
else
	cp /homenvm/lupones/interference/manager .
fi

mkdir -p data
#rm -r data/*
mkdir -p configs
mkdir -p log
mkdir -p graphs


# Check if media streaming is there
ms_copy=$(cat template.mako | grep "media_streaming" | wc -l)


echo $WORKLOADS

for ((REP=${INI_REP};REP<${MAX_REP};REP++)); do
    for MASK in ${MASKS[@]}; do
	while read WL; do
	    WL=$(echo $WL | tr '\-[],' " ")
	    
	    if [ ${#MASKS[@]} -eq 1 ]; then
		ID=$(join_by - ${WL[@]})
	    else
		ID=$(join_by - ${WL[@]})_${MASK}
	    fi
	    
	    CONFIG=configs/${ID}.yaml
	    OUT=data/${ID}_${REP}.csv
	    FIN_OUT=data/${ID}_${REP}_fin.csv
	    TOT_OUT=data/${ID}_${REP}_tot.csv
		TIMES_OUT=data/${ID}_${REP}_times.csv
	    LOG=log/${ID}_${REP}.log
	    MEDIA_DIR=/home/data_shared_folder/media-streaming

	    echo $WL $((REP+1))/${MAX_REP} 0x${MASK}

	    python3 ${DIR}/makoc.py template.mako --lookup "${DIR}/templates" --defs '{apps: ['$(join_by , ${WL[@]})'], mask: '$MASK'}'> ${CONFIG} || exit 1
	    
	    echo ./manager --config ${CONFIG} -o ${OUT} --fin-out ${FIN_OUT} --total-out ${TOT_OUT} --times-out ${TIMES_OUT} --flog-min inf --clog-min ${CLOG_MIN} --log-file $LOG
	    numactl --membind=0 ./manager --config ${CONFIG} -o ${OUT} --fin-out ${FIN_OUT} --total-out ${TOT_OUT} --times-out ${TIMES_OUT} --flog-min inf --clog-min ${CLOG_MIN} --log-file $LOG --monitor-only ${MONITOR}


		if ! $MONITOR; then
	    	# Leave the server and client nodes clean
	    	# Shudown the server VMs
	    	for VM in $(echo $VMs); do
	    		echo "virsh shutdown ${VM}"
	    		virsh shutdown ${VM}
	    	done	    

			sleep 5
			echo "Copying the results from the VMs..."
			sleep 5

			echo "ssh -p 3322 ${_USER}@xpl2.gap.upv.es 'killall -9 run_script_client_native_2.sh &>/dev/null'"
			ssh -p 3322 ${_USER}@xpl2.gap.upv.es 'killall -9 run_script_client_native_2.sh &>/dev/null'
			
			# Iterate over Vms
			appNum=0
			for VM in $(echo $VMs); do
				
				if ! $SPEC; then
					echo "---------------"
					echo $VM
					echo "---------------"

					mkdir -p ./data/${VM}_${ID}_${REP}/
					scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/* ./data/${VM}_${ID}_${REP}/
					cp /homenvm/dsf_${VM}/server_log* ./data/${VM}_${ID}_${REP}/

					#mv ./data/${VM}_${ID}_${REP}/ ./data/${appNum}_${arr[${VM}]}_${REP}/

					if [ $ms_copy -gt 0 ]; then
						cp /homenvm/dsf_${VM}/STARTED ./data/${VM}_${ID}_${REP}/server_trace
						scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/media-streaming/output/* ./data/${VM}_${ID}_${REP}/

						for res in `ls ./data/${VM}_${ID}_${REP}/ | grep result`; do
						   echo "cat ./data/${VM}_${ID}_${REP}/$res | grep transfer | cut -d " " -f 7 >> ./data/${VM}_${ID}_${REP}/transfer_lat_all.txt"
						   cat ./data/${VM}_${ID}_${REP}/$res | grep transfer | cut -d " " -f 7 >> ./data/${VM}_${ID}_${REP}/transfer_lat_all.txt
						   cat ./data/${VM}_${ID}_${REP}/$res | grep response | cut -d " " -f 5 >> ./data/${VM}_${ID}_${REP}/response_lat_all.txt
						done

						##for i in $(seq 0 $(($CLIENT_NUM-1))); do
							
						##   scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/lats_${i}.txt ./data/${VM}_${ID}_${REP}_lats_${i}.txt
						##   scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/latsEcho_${i}.txt ./data/${VM}_${ID}_${REP}_latsEcho_${i}.txt				  				  
						##   scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/client_output_${i}.txt ./data/${VM}_${ID}_${REP}_client_output_${i}.txt				    
							
						##   # if [ -d "$MEDIA_DIR" ]; then
						##      if [ $ms_copy -gt 0 ]; then
						##      scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/media-streaming/output/result$((i+1)).log ./data/${VM}_${ID}_${REP}_result_${i}.txt
						##      fi
						##   for j in `ls ./data | grep result`; do
						##	   cat ./data/$j | grep transfer | cut -d " " -f 7 >> ./data/${VM}_${ID}_${REP}_transfer_lat_all.txt
						##      cat ./data/$j | grep response | cut -d " " -f 5 >> ./data/${VM}_${ID}_${REP}_response_lat_all.txt 
						##   done 
						##   #fi
						##   #python3 ${DIR}/graphLats.py ./data/${ID}_${REP}_lats_${i}.txt ${ID} ${REP} ./graphs/
						##done 			   
						##scp -P 3322 ${_USER}@xpl2.gap.upv.es:/home/dsf_${VM}/client_log.txt ./data/${VM}_${ID}_${REP}_client_log.txt
					fi

					mv ./data/${VM}_${ID}_${REP}/ ./data/${appNum}_${arr[${VM}]}_${REP}/
					mv ./data/${arr[${VM}]}_${appNum}_model_results.csv ./data/${appNum}_${arr[${VM}]}_${REP}_model_results.csv
					mv ./data/${arr[${VM}]}_${appNum}_catpol.csv ./data/${appNum}_${arr[${VM}]}_${REP}_catpol.csv
					
				fi
				appNum=$((appNum+1))
				##cp /homenvm/dsf_${VM}/server_log.txt ./data/${VM}_${ID}_${REP}_server_log.txt
			done
		fi
        
        # Josue: not sure what it removes
		#rm -r run 
		
		# Comment if using a network benchmark that don't print responses per second. 
		#if ! $SPEC; then
        #	python3 ${DIR}/parseResp.py ./data/${ID}_${REP}_server_log.txt 1 ./data/${ID}_${REP}.csv > ./data/${ID}_${REP}_server_throughput.txt 
		#fi

		sleep 25
	    
	done < $WORKLOADS
    done
done

echo 1200000 | tee /sys/devices/system/cpu/cpufreq/policy*/scaling_min_freq > /dev/null

