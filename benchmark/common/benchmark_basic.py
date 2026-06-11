import subprocess
import time
import os
import signal
import re
import statistics
import threading

# --- CONFIGURACIÓN DE RUTAS ---
HOME = os.path.expanduser("~")
OPENDDS_ROOT = os.path.join(HOME, "DDS/DDS/OpenDDS-install/OpenDDS-3.33.0")

# --- DEFINICIÓN DE MIDDLEWARES ---
MIDDLEWARES = {
    "OpenDDS": {
        "cwd": os.path.join(HOME, "DDS/DDS/benchmark/OpenDDS/basic"),
        # OpenDDS necesita shell=True para el 'source', así que monitorizaremos a sus hijos
        "shell": True,
        "sub_cmd": f"source {OPENDDS_ROOT}/setenv.sh && exec ./subscriber -DCPSConfigFile rtps.ini",
        "pub_cmd": f"source {OPENDDS_ROOT}/setenv.sh && exec ./publisher -DCPSConfigFile rtps.ini",
    },
    "FastDDS": {
        "cwd": os.path.join(HOME, "DDS/DDS/benchmark/FastDDS/basic/build"),
        # FastDDS y Cyclone no necesitan shell, pasamos el comando como lista
        "shell": False,
        "sub_cmd": ["./subscriber"],
        "pub_cmd": ["./publisher"],
    },
    "CycloneDDS": {
        "cwd": os.path.join(HOME, "DDS/DDS/benchmark/CycloneDDS/basic/build"),
        "shell": False,
        "sub_cmd": ["./subscriber"],
        "pub_cmd": ["./publisher"],
    }
}

TEST_DURATION = 10 

def get_process_stats(pid):
    """
    Obtiene uso de RAM (MB) y CPU (%) de un PID y sus hijos directos.
    Retorna (mem_mb, cpu_percent)
    """
    total_mem = 0.0
    total_cpu = 0.0
    
    # Lista de PIDs a revisar: el padre + sus hijos
    pids_to_check = [pid]
    
    try:
        # Buscar hijos usando pgrep (útil cuando usamos shell=True)
        children = subprocess.run(
            ["pgrep", "-P", str(pid)], 
            capture_output=True, text=True
        ).stdout.strip().split()
        
        for child in children:
            pids_to_check.append(int(child))
            
    except Exception:
        pass # Si falla pgrep, solo medimos al padre

    # Medir cada PID encontrado
    for p in pids_to_check:
        try:
            # rss= memoria física en KB, %cpu= uso cpu
            # Usamos --no-headers para facilitar el parseo
            res = subprocess.run(
                ["ps", "-p", str(p), "-o", "rss=,%cpu="], 
                capture_output=True, text=True
            )
            if res.returncode == 0 and res.stdout.strip():
                parts = res.stdout.strip().split()
                if len(parts) >= 2:
                    total_mem += float(parts[0]) / 1024.0 # KB a MB
                    total_cpu += float(parts[1])
        except Exception:
            pass # El proceso pudo haber muerto entre el pgrep y el ps

    return total_mem, total_cpu

def monitor_thread_func(pid, stop_event, stats_list):
    """Función del hilo que monitoriza recursos constantemente"""
    while not stop_event.is_set():
        mem, cpu = get_process_stats(pid)
        # Solo guardamos si hay consumo real (evitamos ceros iniciales falsos)
        if mem > 0:
            stats_list.append((mem, cpu))
        time.sleep(0.5)

def run_benchmark(mw_name, config):
    print(f"\n{'='*70}")
    print(f" INICIANDO BENCHMARK: {mw_name}")
    print(f"{'='*70}")

    if not os.path.exists(config['cwd']):
        print(f"ERROR: No existe el directorio {config['cwd']}")
        return

    # 1. ARRANCAR SUBSCRIBER
    print(f" -> [SUB] Iniciando Subscriber...")
    
    # Ajuste del comando según si usamos shell o no
    cmd_sub = config['sub_cmd']
    if config['shell']: 
        # Si es shell, usamos bash explícito
        cmd_sub = f"bash -c '{config['sub_cmd']}'"

    try:
        sub_proc = subprocess.Popen(
            cmd_sub, 
            cwd=config['cwd'],
            shell=config['shell'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, 
            text=True,
            preexec_fn=os.setsid 
        )
    except Exception as e:
        print(f"Error lanzando subscriber: {e}")
        return

    # Monitor Subscriber
    sub_stats = []
    stop_sub_monitor = threading.Event()
    sub_thread = threading.Thread(target=monitor_thread_func, args=(sub_proc.pid, stop_sub_monitor, sub_stats))
    sub_thread.start()

    print(" -> Esperando descubrimiento (2s)...")
    time.sleep(2)

    # 2. ARRANCAR PUBLISHER
    print(f" -> [PUB] Iniciando Publisher...")
    
    cmd_pub = config['pub_cmd']
    if config['shell']:
        cmd_pub = f"bash -c '{config['pub_cmd']}'"

    try:
        pub_proc = subprocess.Popen(
            cmd_pub,
            cwd=config['cwd'],
            shell=config['shell'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid 
        )
    except Exception as e:
        print(f"Error lanzando publisher: {e}")
        return

    # Monitor Publisher
    pub_stats = []
    stop_pub_monitor = threading.Event()
    pub_thread = threading.Thread(target=monitor_thread_func, args=(pub_proc.pid, stop_pub_monitor, pub_stats))
    pub_thread.start()

    # 3. RECOLECTAR DATOS
    latencies = []
    start_time = time.time()
    
    print(" -> Recibiendo datos ", end="")
    try:
        while (time.time() - start_time) < TEST_DURATION:
            line = sub_proc.stdout.readline()
            if not line:
                break
            
            match = re.search(r"latency_ns=(\d+)", line)
            if match:
                ns = int(match.group(1))
                latencies.append(ns / 1000.0) 
                
                if len(latencies) % 1000 == 0:
                    print(".", end="", flush=True)

    except KeyboardInterrupt:
        print("\nInterrumpido por usuario.")
    finally:
        print("\n -> Finalizando procesos...")
        
        # Matar grupos de procesos
        try: os.killpg(os.getpgid(sub_proc.pid), signal.SIGTERM)
        except: pass
        try: os.killpg(os.getpgid(pub_proc.pid), signal.SIGTERM)
        except: pass
        
        # Limpieza de seguridad
        subprocess.run(["pkill", "-f", "publisher"], stderr=subprocess.DEVNULL) 
        subprocess.run(["pkill", "-f", "subscriber"], stderr=subprocess.DEVNULL) 

        stop_sub_monitor.set()
        stop_pub_monitor.set()
        sub_thread.join()
        pub_thread.join()

    # 4. RESULTADOS
    if latencies:
        avg_lat = statistics.mean(latencies)
        min_lat = min(latencies)
        max_lat = max(latencies)
        stdev_lat = statistics.stdev(latencies) if len(latencies) > 1 else 0
        
        # Calcular recursos (quitando valores nulos si los hubiera)
        sub_mem = statistics.mean([m for m, c in sub_stats]) if sub_stats else 0
        sub_cpu = statistics.mean([c for m, c in sub_stats]) if sub_stats else 0
        pub_mem = statistics.mean([m for m, c in pub_stats]) if pub_stats else 0
        pub_cpu = statistics.mean([c for m, c in pub_stats]) if pub_stats else 0

        print(f"\n RESULTADOS {mw_name}:")
        print(f" ------------------------------------------------")
        print(f"  Mensajes procesados: {len(latencies)}")
        print(f" ------------------------------------------------")
        print(f"  LATENCIA:")
        print(f"    Media:      {avg_lat:.2f} us")
        print(f"    Mínima:     {min_lat:.2f} us")
        print(f"    Máxima:     {max_lat:.2f} us")
        print(f"    Jitter:     {stdev_lat:.2f} us")
        print(f" ------------------------------------------------")
        print(f"  RECURSOS (Promedio):")
        print(f"    PUB CPU:    {pub_cpu:.2f} %  | RAM: {pub_mem:.2f} MB")
        print(f"    SUB CPU:    {sub_cpu:.2f} %  | RAM: {sub_mem:.2f} MB")
        print(f" ------------------------------------------------")
    else:
        print("\n [!] No se recibieron datos.")

def main():
    # Ejecutar secuencialmente
    run_benchmark("OpenDDS", MIDDLEWARES["OpenDDS"])
    time.sleep(3)
    run_benchmark("FastDDS", MIDDLEWARES["FastDDS"])
    time.sleep(3)
    run_benchmark("CycloneDDS", MIDDLEWARES["CycloneDDS"])

if __name__ == "__main__":
    main()
