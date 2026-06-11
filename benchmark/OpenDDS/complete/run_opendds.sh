#!/bin/bash
source /home/hblazquez/OpenDDS_WS/OpenDDS/setenv.sh
./vehicle_node -DCPSConfigFile rtps.ini "$@"
