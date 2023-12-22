#!/bin/bash

# Define folder path (inside VM)
# E.g., /home/VM_USER
FOLDER_PATH="XXXXX/XXXXX"


sudo mount data_shared_folder ${FOLDER_PATH}/data_shared_folder -t 9p -o trans=virtio,version=9p2000.L
sudo mount server_scripts ${FOLDER_PATH}/server_scripts -t 9p -o trans=virtio,version=9p2000.L

eval "$@"

