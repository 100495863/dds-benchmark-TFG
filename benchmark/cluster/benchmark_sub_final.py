import subprocess
import time
import os
import signal
import re
import statistics
import threading
import select

HOME = "/home/hblazquez"

MIDDLEWARES = {
    "CycloneDDS": {
        "cwd": os.path.join(HOME, "benchmark/CycloneDDS/complete/build"),
        "cmd_base": ["./vehicle_node"]
    },
    "FastDDS": {
        "cwd": os.path.join(HOME, "benchmark/FastDDS/complete/build"),
        "cmd_base": ["./vehicle_node"]
    },
    "OpenDDS": {
        "cwd": os.path.join(HOME, "benchmark/OpenDDS/complete"),
        "cmd_base": ["./run_opendds.sh", "-DCPSConfigFile", "rtps.ini"]
    }
}

def get_custom_env(mw_name):
    return os.environ.copy()

def get_process_stats(pid):
    total_mem, total_cpu = 0.0, 0.0
    pids_to_check = [pid]
    try:
        children = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True).stdout.strip().split()
        pids_to_check.extend([int(c) for c in children])
    except: pass
    for p in pids_to_check:
        try:
            res = subprocess.run(["ps", "-p", str(p), "-o", "rss=,%cpu="], capture_output=True, text=True)
            if res.returncode == 0 and res.stdout.strip():
                parts = res.stdout.strip().split()
                if len(parts) >= 2:
                    total_mem += float(parts[0]) / 1024.0
                    total_cpu += float(parts[1])
        except: pass
    return total_mem, total_cpu

def monitor_resources(pid, stop_event, stats_list):
    while not stop_event.is_set():
        mem, cpu = get_process_stats(pid)
        if mem > 0: stats_list.append((mem, cpu))
        time.sleep(0.5)

def monitor_multi_resources(pids, stop_event, stats_list):
    while not stop_event.is_set():
        total_mem, total_cpu = 0.0, 0.0
        for pid in pids:
            mem, cpu = get_process_stats(pid)
            total_mem += mem
            total_cpu += cpu
        if total_mem > 0: stats_list.append((total_mem, total_cpu))
        time.sleep(0.5)

def test_1_throughput_sub(mw_name, config, payload_size):
    print(f"\n--- TEST 1: THROUGHPUT - Payload: {payload_size} bytes ---")
    sub_stats = []
    stop_monitor = threading.Event()
    throughputs = []
    env = get_custom_env(mw_name)

    cmd_sub = config['cmd_base'] + ["--mode", "lidar", "--role", "sub"]
    proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=env)
    
    t_sub = threading.Thread(target=monitor_resources, args=(proc_sub.pid, stop_monitor, sub_stats))
    t_sub.start()

    print("   Escuchando datos de ancho de banda durante 7 segundos...")
    start_time = time.time()
    try:
        while (time.time() - start_time) < 7:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 1.0)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Throughput:" in line:
                    match = re.search(r"Throughput:\s+([0-9.]+)\s+MB/s", line)
                    if match: throughputs.append(float(match.group(1)))
    finally:
        stop_monitor.set()
        os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)
        t_sub.join()

    avg_mbps = statistics.mean(throughputs) if throughputs else 0.0
    cpu_sub = statistics.mean([c for m, c in sub_stats]) if sub_stats else 0.0
    mem_sub = max([m for m, c in sub_stats]) if sub_stats else 0.0 # Usamos MAX para ver el pico de RAM
    print(f"   -> Media recibida: {avg_mbps:.2f} MB/s | CPU Sub: {cpu_sub:.1f}% | RAM Pico Sub: {mem_sub:.1f} MB")

def test_2_qos_late_joiner_sub(mw_name, config, delay_seconds):
    print(f"\n--- TEST 2: LATE JOINER - Retraso: {delay_seconds} segundos ---")
    env = get_custom_env(mw_name)
    proc_sub = None
    latencies = []
    
    try:
        time.sleep(delay_seconds) # Esperamos antes de unirnos
        cmd_sub = config['cmd_base'] + ["--mode", "emergency", "--role", "sub"]
        proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=env)

        start_time = time.time()
        while (time.time() - start_time) < 4:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 0.5)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Latencia real o de historial:" in line:
                    match = re.search(r"historial:\s+([0-9.]+)\s+ms", line)
                    if match: latencies.append(float(match.group(1)))
    finally:
        if proc_sub: os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)

    print(f"   -> Recuperados {len(latencies)} mensajes del historial tras {delay_seconds}s de espera.")

def test_3_scalability_sub(mw_name, config, num_nodes):
    print(f"\n--- TEST 3: ESCALABILIDAD DISTRIBUIDA - {num_nodes} Suscriptores ---")
    procs, pids, stats = [], [], []
    stop_monitor = threading.Event()
    env = get_custom_env(mw_name)

    # Lanzamos N suscriptores
    for i in range(num_nodes):
        cmd = config['cmd_base'] + ["--mode", "telemetry", "--role", "sub", "--id", f"Car_{i}"]
        p = subprocess.Popen(cmd, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=env)
        procs.append(p)
        pids.append(p.pid)

    t = threading.Thread(target=monitor_multi_resources, args=(pids, stop_monitor, stats))
    t.start()

    print(f"   [!] Escuchando ráfaga de descubrimiento y telemetría durante 12 segundos...")
    time.sleep(12)

    stop_monitor.set()
    for p in procs:
        try: os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        except: pass
    t.join()

    if stats:
        max_total_cpu = max([c for m, c in stats])
        max_total_ram = max([m for m, c in stats])
        print(f"   -> PICO TOTAL del sistema (Recepción): CPU {max_total_cpu:.1f}% | RAM {max_total_ram:.1f} MB")
    else:
        print("   -> Error recolectando métricas.")

def main():
    print("=========================================================")
    print(" NODO SUSCRIPTOR - BENCHMARK DDS DISTRIBUIDO FINAL ")
    print("=========================================================")

    payload_sizes = [256, 1024, 4096, 16384, 65536, 100000]
    late_joiner_delays = [1, 3, 5, 10, 20]
    scalability_nodes = [5, 10, 20, 30]

    for mw_name, config in MIDDLEWARES.items():
        print(f"\n\n{'#'*60}")
        print(f"  PREPARANDO MIDDLEWARE: {mw_name.upper()}")
        print(f"{'#'*60}")
        input(">>> PRESIONA [ENTER] A LA VEZ EN AMBAS MÁQUINAS PARA EMPEZAR <<<")

        print("\n[BATERÍA 1] Evaluando Ancho de Banda y Fragmentación...")
        for size in payload_sizes:
            test_1_throughput_sub(mw_name, config, payload_size=size)

        print("\n[BATERÍA 2] Evaluando Memoria Histórica a través de Red (Late Joiner)...")
        for delay in late_joiner_delays:
            test_2_qos_late_joiner_sub(mw_name, config, delay_seconds=delay)

        print("\n[BATERÍA 3] Evaluando Tormenta de Descubrimiento (Escalabilidad)...")
        for nodes in scalability_nodes:
            test_3_scalability_sub(mw_name, config, num_nodes=nodes)
            time.sleep(2) # Pausa para limpiar sockets

if __name__ == "__main__":
    main()
