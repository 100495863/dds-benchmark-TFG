import os
import subprocess
import time
import re

TEST_DURATION = 35 
COOLDOWN = 5

HOME = os.path.expanduser("~")
MIDDLEWARES = [
    {
        "name": "FastDDS",
        "dir": f"{HOME}/benchmark/FastDDS/audio_video/build",
        "cmd": ["stdbuf", "-oL", "./streaming_node", "sub"] # stdbuf fuerza a escribir en el .log al instante
    },
    {
        "name": "CycloneDDS",
        "dir": f"{HOME}/benchmark/CycloneDDS/audio_video/build",
        "cmd": ["stdbuf", "-oL", "./streaming_node", "sub"]
    },
    {
        "name": "OpenDDS",
        "dir": f"{HOME}/benchmark/OpenDDS/audio_video",
        "cmd": ["stdbuf", "-oL", "./streaming_node", "sub", "-DCPSConfigFile", f"{HOME}/benchmark/OpenDDS/audio_video/rtps.ini"]
    }
]

def analyze_log(filename, mw_name):
    if not os.path.exists(filename) or os.path.getsize(filename) == 0:
        print(f"| {mw_name:<11} | ⚠️ ARCHIVO LOG VACÍO O NO ENCONTRADO ⚠️".ljust(101) + "|")
        return
    
    throughputs = []
    jitters = []
    video_latencies = []
    video_lost = 0
    audio_lost = 0
    
    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # V.DEO ignora el acento para evitar fallos de codificación
            if "DEO]" in line:
                match_mb = re.search(r"Throughput:\s*([\d.]+)", line)
                match_lost = re.search(r"Perdidos:\s*(\d+)", line)
                match_lat = re.search(r"Latencia:\s*([\d.]+)", line)
                
                if match_mb and float(match_mb.group(1)) > 0:
                    throughputs.append(float(match_mb.group(1)))
                if match_lost:
                    video_lost = int(match_lost.group(1))
                if match_lat and float(match_lat.group(1)) > 0:
                    video_latencies.append(float(match_lat.group(1)))
            
            elif "AUDIO]" in line:
                match_jitter = re.search(r"Max Jitter:\s*([\d.]+)", line)
                match_lost = re.search(r"Perdidos:\s*(\d+)", line)
                if match_jitter:
                    jitters.append(float(match_jitter.group(1)))
                if match_lost:
                    audio_lost = int(match_lost.group(1))
                    
    avg_throughput = sum(throughputs)/len(throughputs) if throughputs else 0.0
    avg_latency = sum(video_latencies)/len(video_latencies) if video_latencies else 0.0
    max_jitter = max(jitters) if jitters else 0.0
    
    # Formato ajustado para la nueva columna de Latencia
    print(f"| {mw_name:<11} | {avg_throughput:>10.2f} MB/s | {video_lost:>17} | {avg_latency:>10.2f} ms | {max_jitter:>9.2f} ms | {audio_lost:>17} |")

def main():
    print("🎧 Iniciando Benchmark de Streaming Multimedia (SUSCRIPTOR) 🎧\n")
    
    for mw in MIDDLEWARES:
        log_file = f"{mw['name']}_streaming.log"
        print(f"[{mw['name']}] Escuchando... guardando datos en {log_file}")
        
        with open(log_file, "w") as f:
            proc = subprocess.Popen(mw["cmd"], cwd=mw["dir"], stdout=f, stderr=subprocess.STDOUT)
            time.sleep(TEST_DURATION)
            proc.terminate()
            proc.wait()
            
            # LIMPIEZA DRÁSTICA: Matar procesos residuales antes de cambiar de middleware
            os.system("killall -9 streaming_node 2>/dev/null")
            
        print(f"[{mw['name']}] Recolección finalizada. Esperando {COOLDOWN}s...\n")
        time.sleep(COOLDOWN)
        
    print("\n" + "="*103)
    print("📊 RESULTADOS FINALES DEL BENCHMARK DE STREAMING MULTIMEDIA CON ESTRÉS 📊".center(103))
    print("="*103)
    print(f"| {'Middleware':<11} | {'Avg Throughput':<14} | {'Video Lost Frames':<17} | {'Avg Latency':<13} | {'Max Jitter':<12} | {'Audio Lost Frames':<17} |")
    print("-" * 103)
    
    for mw in MIDDLEWARES:
        analyze_log(f"{mw['name']}_streaming.log", mw["name"])
        
    print("="*103 + "\n")

if __name__ == "__main__":
    main()
