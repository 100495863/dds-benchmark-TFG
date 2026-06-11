#!/bin/bash
# 1. Cargar el entorno de OpenDDS y ACE/TAO
source ~/OpenDDS_WS/OpenDDS/setenv.sh

# 2. Ejecutar el nodo pasándole la configuración RTPS y cualquier argumento (ej. "sub")
./streaming_node -DCPSConfigFile rtps.ini "$@"
