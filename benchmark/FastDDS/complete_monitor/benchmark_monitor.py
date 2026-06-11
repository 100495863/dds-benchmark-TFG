import subprocess
import time
import os
import signal
import re
import statistics
import threading
import select

HOME = os.path.expanduser("~")

# Solo apuntamos al entorno aislado de monitorización
CONFIG = {
    "cwd": os.path.join(HOME, "DDS/DDS/benchmark/FastDDS/complete_monitor/build"),
    "cmd_base": ["./vehicle_node"],
    "cmd_monitor": ["./monitor"]
}

def get_process_stats(pid):
    """Obtiene memoria física (MB) y CPU (%) de un PID y sus hijos."""
    total_mem = 0.0
    total_cpu = 0.0
    pids_to_check = [pid]
    try:
        children = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True).stdout.strip().split()
        pids_to_check.extend([int(c) for c in children])
    except:
        pass
    for p in pids_to_check:
        try:
            res = subprocess.run(["ps", "-p", str(p), "-o", "rss=,%cpu="], capture_output=True, text=True)
            if res.returncode == 0 and res.stdout.strip():
                parts = res.stdout.strip().split()
                if len(parts) >= 2:
                    total_mem += float(parts[0]) / 1024.0
                    total_cpu += float(parts[1])
        except:
            pass
    return total_mem, total_cpu

def monitor_resources(pid, stop_event, stats_list):
    """Hilo para recopilar recursos del sistema operativo."""
    while not stop_event.is_set():
        mem, cpu = get_process_stats(pid)
        if mem > 0:
            stats_list.append((mem, cpu))
        time.sleep(0.5)

def inject_network_chaos():
    print("   [!] Inyectando caos en la interfaz loopback (10% pérdida, 50ms latencia)...")
    subprocess.run(["sudo", "tc", "qdisc", "del", "dev", "lo", "root"], stderr=subprocess.DEVNULL)
    subprocess.run(["sudo", "tc", "qdisc", "add", "dev", "lo", "root", "netem", "loss", "10%", "delay", "50ms", "10ms"], stderr=subprocess.DEVNULL)

def remove_network_chaos():
    print("   [!] Restaurando red a la normalidad...")
    subprocess.run(["sudo", "tc", "qdisc", "del", "dev", "lo", "root"], stderr=subprocess.DEVNULL)


def launch_dds_monitor(test_name, duration):
    """Lanza el monitor configurado para volcar datos solo 1 vez al final de la prueba."""
    # El intervalo será la duración de la prueba para evitar generar decenas de JSONs
    cmd = CONFIG['cmd_monitor'] + ["--interval", str(duration), "--output", f"dump_{test_name}"]
    print(f"   [Monitor] Iniciado (volcará datos en dump_{test_name}_*.json)")
    return subprocess.Popen(cmd, cwd=CONFIG['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid)


def test_1_throughput(payload_size):
    duration = 10
    print(f"\n--- TEST 1: THROUGHPUT (LIDAR) - Payload: {payload_size} bytes ---")

    sub_stats, pub_stats = [], []
    stop_monitor = threading.Event()
    throughputs = []

    # 0. Arrancamos el Monitor FastDDS
    proc_monitor = launch_dds_monitor(f"throughput_{payload_size}", duration)
    time.sleep(1) # Dar tiempo a que el monitor escuche la red

    # 1. Arrancamos el Subscriber
    cmd_sub = CONFIG['cmd_base'] + ["--mode", "lidar", "--role", "sub"]
    proc_sub = subprocess.Popen(cmd_sub, cwd=CONFIG['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid)
    t_sub = threading.Thread(target=monitor_resources, args=(proc_sub.pid, stop_monitor, sub_stats))
    t_sub.start()
    time.sleep(1)

    # 2. Arrancamos el Publisher
    cmd_pub = CONFIG['cmd_base'] + ["--mode", "lidar", "--role", "pub", "--payload", str(payload_size)]
    proc_pub = subprocess.Popen(cmd_pub, cwd=CONFIG['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid)
    t_pub = threading.Thread(target=monitor_resources, args=(proc_pub.pid, stop_monitor, pub_stats))
    t_pub.start()

    print(f"   Recolectando datos durante {duration} segundos...")
    start_time = time.time()
    try:
        while (time.time() - start_time) < duration:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 1.0)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Throughput:" in line:
                    match = re.search(r"Throughput:\s+([0-9.]+)\s+MB/s", line)
                    if match:
                        mbps = float(match.group(1))
                        throughputs.append(mbps)
                        print(f"     -> {mbps} MB/s")
    except KeyboardInterrupt:
        pass
    finally:
        stop_monitor.set()
        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_monitor.pid), signal.SIGINT) # SIGINT para que el monitor guarde el JSON final
        t_sub.join()
        t_pub.join()

    avg_mbps = statistics.mean(throughputs) if throughputs else 0.0
    cpu_pub = statistics.mean([c for m, c in pub_stats]) if pub_stats else 0.0
    print(f"   RESULTADO: Media {avg_mbps:.2f} MB/s | CPU Pub: {cpu_pub:.1f}%")

def test_2_qos_late_joiner():
    duration = 10
    print(f"\n--- TEST 2: QOS AVANZADO (LATE JOINER) ---")
    
    proc_monitor = launch_dds_monitor("qos_late_joiner", duration)
    time.sleep(1)

    print("   1. Arrancando Publisher de Emergencia...")
    cmd_pub = CONFIG['cmd_base'] + ["--mode", "emergency", "--role", "pub"]
    proc_pub = subprocess.Popen(cmd_pub, cwd=CONFIG['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid)

    print("   2. Esperando 5 segundos (simulando retraso)...")
    time.sleep(5)

    print("   3. Arrancando Subscriber...")
    cmd_sub = CONFIG['cmd_base'] + ["--mode", "emergency", "--role", "sub"]
    proc_sub = subprocess.Popen(cmd_sub, cwd=CONFIG['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid)

    latencies = []
    start_time = time.time()
    try:
        while (time.time() - start_time) < 3:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 0.5)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Latencia real o de historial:" in line:
                    match = re.search(r"historial:\s+([0-9.]+)\s+ms", line)
                    if match:
                        latencies.append(float(match.group(1)))
    finally:
        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_monitor.pid), signal.SIGINT)

    print(f"   RESULTADO: Se recibieron {len(latencies)} mensajes del historial.")
    if latencies:
        print(f"   Latencias de recuperación: {[round(l, 2) for l in latencies]} ms")

def test_3_network_chaos():
    duration = 6
    print(f"\n--- TEST 3: CAOS DE RED (PERDIDA DE PAQUETES) ---")

    proc_monitor = launch_dds_monitor("network_chaos", duration)
    time.sleep(1)

    inject_network_chaos()
    time.sleep(1)

    try:
        print("   Ejecutando Publisher y Subscriber bajo red degradada...")
        cmd_sub = CONFIG['cmd_base'] + ["--mode", "emergency", "--role", "sub"]
        proc_sub = subprocess.Popen(cmd_sub, cwd=CONFIG['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid)

        time.sleep(1)

        cmd_pub = CONFIG['cmd_base'] + ["--mode", "emergency", "--role", "pub"]
        proc_pub = subprocess.Popen(cmd_pub, cwd=CONFIG['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid)

        latencies = []
        start_time = time.time()

        while (time.time() - start_time) < 5:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 0.5)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Latencia real o de historial:" in line:
                    match = re.search(r"historial:\s+([0-9.]+)\s+ms", line)
                    if match:
                        latencies.append(float(match.group(1)))

        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)

        print(f"   RESULTADO: Se lograron entregar {len(latencies)}/5 mensajes bajo caos.")
        if latencies:
            print(f"   Latencia media con red degradada: {statistics.mean(latencies):.2f} ms")

    finally:
        remove_network_chaos()
        os.killpg(os.getpgid(proc_monitor.pid), signal.SIGINT)

def test_4_scalability(num_nodes=20):
    duration = 12
    print(f"\n--- TEST 4: ESCALABILIDAD ENJAMBRE ({num_nodes} NODOS) ---")

    proc_monitor = launch_dds_monitor("scalability", duration)
    time.sleep(1)

    procs = []
    stats = []
    stop_monitor = threading.Event()

    print(f"   Arrancando {num_nodes} publicadores de Telemetría a la vez...")
    for i in range(num_nodes):
        cmd = CONFIG['cmd_base'] + ["--mode", "telemetry", "--role", "pub", "--id", f"Car_{i}"]
        p = subprocess.Popen(cmd, cwd=CONFIG['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid)
        procs.append(p)

        t = threading.Thread(target=monitor_resources, args=(p.pid, stop_monitor, stats))
        t.start()

    print("   Dejando que la red de descubrimiento se estabilice (10s)...")
    time.sleep(10)

    stop_monitor.set()
    print("   Apagando enjambre...")
    for p in procs:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        except:
            pass
            
    os.killpg(os.getpgid(proc_monitor.pid), signal.SIGINT)

    total_cpu = sum([c for m, c in stats]) / len(procs) if procs else 0
    total_ram = sum([m for m, c in stats]) / len(procs) if procs else 0
    print(f"   RESULTADO ESTIMADO: CPU Total {total_cpu:.1f}% | RAM Total {total_ram:.1f} MB")

def main():
    print("=========================================================")
    print(" ADVANCED MONITOR BENCHMARK: FASTDDS (ISOLATED) ")
    print("=========================================================")

    # Limpiamos JSONs antiguos para no ensuciar
    print("\n[!] Limpiando archivos JSON antiguos...")
    subprocess.run(f"rm -f {CONFIG['cwd']}/dump_*.json", shell=True)

    test_1_throughput(payload_size=100000)
    time.sleep(2)
    test_1_throughput(payload_size=5000000)
    time.sleep(2)
    test_2_qos_late_joiner()
    time.sleep(2)
    test_3_network_chaos()
    time.sleep(2)
    test_4_scalability(num_nodes=20)
    
    print("\n>>> Fin de la monitorización aislada.")
    print(f">>> Revisa la carpeta {CONFIG['cwd']} para ver los archivos JSON generados.")

if __name__ == "__main__":
    main()
