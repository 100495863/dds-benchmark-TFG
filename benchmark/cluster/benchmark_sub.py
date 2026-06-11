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
    "CycloneDDS": { "cwd": os.path.join(HOME, "benchmark/CycloneDDS/complete/build"), "cmd_base": ["./vehicle_node"] },
    "FastDDS": { "cwd": os.path.join(HOME, "benchmark/FastDDS/complete/build"), "cmd_base": ["./vehicle_node"] },
    "OpenDDS": { "cwd": os.path.join(HOME, "benchmark/OpenDDS/complete"), "cmd_base": ["./run_opendds.sh", "-DCPSConfigFile", "rtps.ini"] }
}

def get_custom_env(): return os.environ.copy()

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

def test_1_throughput(mw_name, config, payload_size):
    print(f"\n--- TEST 1: THROUGHPUT - Payload: {payload_size} bytes ---")
    sub_stats, throughputs = [], []
    stop_monitor = threading.Event()
    
    cmd_sub = config['cmd_base'] + ["--mode", "lidar", "--role", "sub"]
    proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=get_custom_env())
    t_sub = threading.Thread(target=monitor_resources, args=(proc_sub.pid, stop_monitor, sub_stats))
    t_sub.start()

    print("   Escuchando datos de ancho de banda durante 7 segundos...")
    start_time = time.time()
    try:
        while (time.time() - start_time) < 7: # 7s para dar tiempo al pub a arrancar
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
    mem_sub = statistics.mean([m for m, c in sub_stats]) if sub_stats else 0.0
    print(f"   -> Media recibida: {avg_mbps:.2f} MB/s | CPU Sub: {cpu_sub:.1f}% | RAM Sub: {mem_sub:.1f} MB")

def test_2_qos_late_joiner(mw_name, config, delay_seconds):
    print(f"\n--- TEST 2: LATE JOINER - Retraso: {delay_seconds} segundos ---")
    proc_sub = None
    try:
        # El suscriptor espera X segundos antes de conectarse
        time.sleep(delay_seconds)
        cmd_sub = config['cmd_base'] + ["--mode", "emergency", "--role", "sub"]
        proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=get_custom_env())

        latencies = []
        start_time = time.time()
        while (time.time() - start_time) < 4: # Escucha durante 4s tras unirse
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

def main():
    print("=========================================================")
    print(" NODO SUSCRIPTOR - BENCHMARK DDS DISTRIBUIDO ")
    print("=========================================================")

    payload_sizes = [1024, 10240, 100000, 500000, 1048576, 5242880, 10485760]
    late_joiner_delays = [1, 3, 5, 10, 20]

    for mw_name, config in MIDDLEWARES.items():
        print(f"\n\n{'#'*60}")
        print(f"  PREPARANDO MIDDLEWARE: {mw_name.upper()}")
        print(f"{'#'*60}")
        input(">>> PRESIONA [ENTER] A LA VEZ EN AMBAS MÁQUINAS PARA EMPEZAR <<<")

        print("\n[BATERÍA 1] Evaluando Ancho de Banda y Carga de CPU...")
        for size in payload_sizes:
            test_1_throughput(mw_name, config, payload_size=size)

        print("\n[BATERÍA 2] Evaluando Memoria Histórica (Late Joiner)...")
        for delay in late_joiner_delays:
            test_2_qos_late_joiner(mw_name, config, delay_seconds=delay)
            time.sleep(1)

if __name__ == "__main__":
    main()
