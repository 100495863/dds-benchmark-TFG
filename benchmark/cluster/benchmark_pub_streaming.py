import os
import subprocess
import time

# Duración de la prueba por cada middleware en segundos
TEST_DURATION = 30
COOLDOWN = 10 # Tiempo de espera entre pruebas para limpiar la red

# Rutas a los ejecutables (ajustadas a tu estructura de directorios)
HOME = os.path.expanduser("~")
MIDDLEWARES = [
    {
        "name": "FastDDS",
        "dir": f"{HOME}/benchmark/FastDDS/audio_video/build",
        "cmd": ["./streaming_node"]
    },
    {
        "name": "CycloneDDS",
        "dir": f"{HOME}/benchmark/CycloneDDS/audio_video/build",
        "cmd": ["./streaming_node"]
    },
    {
        "name": "OpenDDS",
        "dir": f"{HOME}/benchmark/OpenDDS/audio_video",
        "cmd": ["./streaming_node", "-DCPSConfigFile", f"{HOME}/benchmark/OpenDDS/audio_video/rtps.ini"]
    }
]

def main():
    print("🚀 Iniciando Benchmark de Streaming Multimedia (PUBLICADOR) 🚀\n")
    
    for mw in MIDDLEWARES:
        print(f"[{mw['name']}] Iniciando transmisión de Vídeo y Audio...")
        
        # Lanzar el proceso
        proc = subprocess.Popen(mw["cmd"], cwd=mw["dir"])
        
        # Dejarlo publicando durante el tiempo establecido
        time.sleep(TEST_DURATION)
        
        # Matar el proceso
        print(f"[{mw['name']}] Deteniendo transmisión...")
        proc.terminate()
        proc.wait()
        
        # LIMPIEZA DRÁSTICA: Matar cualquier proceso residual de la red
        os.system("killall -9 streaming_node 2>/dev/null")
        
        print(f"[{mw['name']}] Prueba finalizada. Esperando {COOLDOWN}s de enfriamiento...\n")
        time.sleep(COOLDOWN)
        
    print("✅ Todas las transmisiones han finalizado.")

if __name__ == "__main__":
    main()
