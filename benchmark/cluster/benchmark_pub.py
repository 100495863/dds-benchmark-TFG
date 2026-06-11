import subprocess
import time
import os
import signal
import statistics
import threading

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
    pub_stats = []
    stop_monitor = threading.Event()
    
    cmd_pub = config['cmd_base'] + ["--mode", "lidar", "--role", "pub", "--payload", str(payload_size)]
    proc_pub = subprocess.Popen(cmd_pub, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=get_custom_env())
    t_pub = threading.Thread(target=monitor_resources, args=(proc_pub.pid, stop_monitor, pub_stats))
    t_pub.start()

    print("   Enviando datos a máxima velocidad durante 7 segundos...")
    try:
        time.sleep(7) # Mantenemos el publicador vivo 7s
    finally:
        stop_monitor.set()
        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        t_pub.join()

    cpu_pub = statistics.mean([c for m, c in pub_stats]) if pub_stats else 0.0
    mem_pub = statistics.mean([m for m, c in pub_stats]) if pub_stats else 0.0
    print(f"   -> Tráfico inyectado finalizado | CPU Pub: {cpu_pub:.1f}% | RAM Pub: {mem_pub:.1f} MB")

def test_2_qos_late_joiner(mw_name, config, delay_seconds):
    print(f"\n--- TEST 2: LATE JOINER - Esperando a suscriptor ({delay_seconds}s) ---")
    
    cmd_pub = config['cmd_base'] + ["--mode", "emergency", "--role", "pub"]
    proc_pub = subprocess.Popen(cmd_pub, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=get_custom_env())

    try:
        # El publicador arranca de inmediato y se mantiene vivo el tiempo de retraso + 4s de margen
        time.sleep(delay_seconds + 4)
    finally:
        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)

    print(f"   -> Publicador cerrado tras mantener el historial vivo.")

def main():
    print("=========================================================")
    print(" NODO PUBLICADOR - BENCHMARK DDS DISTRIBUIDO ")
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
