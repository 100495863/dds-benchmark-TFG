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

# --- Entorno Limpio ---
def get_custom_env(mw_name):
    """
    Clona el entorno del SO.
    Se ha eliminado la inyección de variables XML para evaluar el comportamiento por defecto.
    """
    env = os.environ.copy()
    return env

# --- Monitorización ---
def get_process_stats(pid):
    """Obtienemos memoria física (MB) y CPU (%) de un PID y sus hijos."""
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
    """Hilo para recopilar recursos."""
    while not stop_event.is_set():
        mem, cpu = get_process_stats(pid)
        if mem > 0:
            stats_list.append((mem, cpu))
        time.sleep(0.5)

def monitor_multi_resources(pids, stop_event, stats_list):
    """Hilo para recopilar recursos de múltiples procesos simultáneamente."""
    while not stop_event.is_set():
        total_mem = 0.0
        total_cpu = 0.0
        for pid in pids:
            mem, cpu = get_process_stats(pid)
            total_mem += mem
            total_cpu += cpu

        if total_mem > 0:
            # Guardamos la foto global del sistema en este instante
            stats_list.append((total_mem, total_cpu))

        time.sleep(0.5)

# --- Control de Red ---
def inject_network_chaos(loss_pct, delay_ms):
    print(f"   [!] Inyectando caos: {loss_pct}% pérdida, {delay_ms}ms latencia...")
    subprocess.run(["sudo", "tc", "qdisc", "del", "dev", "lo", "root"], stderr=subprocess.DEVNULL)
    subprocess.run(["sudo", "tc", "qdisc", "add", "dev", "lo", "root", "netem", "loss", f"{loss_pct}%", "delay", f"{delay_ms}ms", "10ms"], stderr=subprocess.DEVNULL)

def remove_network_chaos():
    print("   [!] Restaurando red a la normalidad...")
    subprocess.run(["sudo", "tc", "qdisc", "del", "dev", "lo", "root"], stderr=subprocess.DEVNULL)

# --- Pruebas ---
def test_1_throughput(mw_name, config, payload_size):
    print(f"\n--- TEST 1: THROUGHPUT - Payload: {payload_size} bytes ---")

    sub_stats, pub_stats = [], []
    stop_monitor = threading.Event()
    throughputs = []

    # Inyectamos el entorno modificado
    custom_env = get_custom_env(mw_name)

    cmd_sub = config['cmd_base'] + ["--mode", "lidar", "--role", "sub"]
    proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=custom_env)
    t_sub = threading.Thread(target=monitor_resources, args=(proc_sub.pid, stop_monitor, sub_stats))
    t_sub.start()
    time.sleep(1)

    cmd_pub = config['cmd_base'] + ["--mode", "lidar", "--role", "pub", "--payload", str(payload_size)]
    proc_pub = subprocess.Popen(cmd_pub, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=custom_env)
    t_pub = threading.Thread(target=monitor_resources, args=(proc_pub.pid, stop_monitor, pub_stats))
    t_pub.start()

    print("   Recolectando datos de ancho de banda durante 5 segundos...")
    start_time = time.time()
    try:
        while (time.time() - start_time) < 5:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 1.0)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Throughput:" in line:
                    match = re.search(r"Throughput:\s+([0-9.]+)\s+MB/s", line)
                    if match:
                        mbps = float(match.group(1))
                        throughputs.append(mbps)
    except KeyboardInterrupt:
        pass
    finally:
        stop_monitor.set()
        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)
        t_sub.join()
        t_pub.join()

    avg_mbps = statistics.mean(throughputs) if throughputs else 0.0
    cpu_pub = statistics.mean([c for m, c in pub_stats]) if pub_stats else 0.0
    mem_pub = statistics.mean([m for m, c in pub_stats]) if pub_stats else 0.0
    print(f"   -> Media: {avg_mbps:.2f} MB/s | CPU Pub: {cpu_pub:.1f}% | RAM Pub: {mem_pub:.1f} MB")

def test_2_qos_late_joiner(mw_name, config, delay_seconds):
    print(f"\n--- TEST 2: LATE JOINER - Retraso: {delay_seconds} segundos ---")

    custom_env = get_custom_env(mw_name)

    cmd_pub = config['cmd_base'] + ["--mode", "emergency", "--role", "pub"]
    proc_pub = subprocess.Popen(cmd_pub, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=custom_env)

    proc_sub = None # Inicializamos a None por si falla antes de crearse

    # EMPEZAMOS EL TRY AQUI, antes del sleep
    try:
        time.sleep(delay_seconds)

        cmd_sub = config['cmd_base'] + ["--mode", "emergency", "--role", "sub"]
        proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=custom_env)

        latencies = []
        start_time = time.time()
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
        # Aseguramos limpieza exista o no proc_sub
        if proc_pub:
            os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        if proc_sub:
            os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)

    print(f"   -> Recuperados {len(latencies)} mensajes del historial tras {delay_seconds}s de espera.")

def test_3_network_chaos(mw_name, config, loss_pct, delay_ms):
    print(f"\n--- TEST 3: CAOS DE RED - {loss_pct}% pérdida, {delay_ms}ms latencia ---")

    # Inyectamos el caos ANTES de que intenten descubrirse
    inject_network_chaos(loss_pct, delay_ms)
    time.sleep(1)

    custom_env = get_custom_env(mw_name)

    try:
        # Arrancamos ambos
        cmd_sub = config['cmd_base'] + ["--mode", "emergency", "--role", "sub"]
        proc_sub = subprocess.Popen(cmd_sub, cwd=config['cwd'], stdout=subprocess.PIPE, text=True, preexec_fn=os.setsid, env=custom_env)

        time.sleep(1) # Leve ventaja al suscriptor

        cmd_pub = config['cmd_base'] + ["--mode", "emergency", "--role", "pub"]
        proc_pub = subprocess.Popen(cmd_pub, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=custom_env)

        latencies = []
        start_time = time.time()

        # AUMENTAMOS EL TIMEOUT A 15 SEGUNDOS.
        # Con 50% de pérdida, el handshake inicial de RTPS puede requerir varios reintentos.
        while (time.time() - start_time) < 15:
            ready, _, _ = select.select([proc_sub.stdout], [], [], 0.5)
            if ready:
                line = proc_sub.stdout.readline()
                if not line: break
                if "Latencia real o de historial:" in line:
                    match = re.search(r"historial:\s+([0-9.]+)\s+ms", line)
                    if match:
                        latencies.append(float(match.group(1)))
                        # Si ya hemos recibido los 5 mensajes del publicador, salimos antes
                        if len(latencies) >= 5:
                            break

        os.killpg(os.getpgid(proc_pub.pid), signal.SIGTERM)
        os.killpg(os.getpgid(proc_sub.pid), signal.SIGTERM)

        if latencies:
            print(f"   -> Entregados {len(latencies)}/5 mensajes. Latencia media: {statistics.mean(latencies):.2f} ms")
        else:
            print(f"   -> Entregados 0 mensajes (Pérdida total de conexión o Discovery fallido)")

    finally:
        remove_network_chaos()

def test_4_scalability(mw_name, config, num_nodes):
    print(f"\n--- TEST 4: ESCALABILIDAD - {num_nodes} NODOS en paralelo ---")

    procs = []
    pids = []
    stats = []
    stop_monitor = threading.Event()

    custom_env = get_custom_env(mw_name)

    # 1. Lanzamos todos los nodos y guardamos sus PIDs
    for i in range(num_nodes):
        cmd = config['cmd_base'] + ["--mode", "telemetry", "--role", "pub", "--id", f"Car_{i}"]
        p = subprocess.Popen(cmd, cwd=config['cwd'], stdout=subprocess.DEVNULL, preexec_fn=os.setsid, env=custom_env)
        procs.append(p)
        pids.append(p.pid)

    # 2. Monitorizamos TODOS los nodos simultáneamente
    t = threading.Thread(target=monitor_multi_resources, args=(pids, stop_monitor, stats))
    t.start()

    print(f"   [!] Esperando 8 segundos para estabilización (Fase de Discovery RTPS)...")
    time.sleep(8)

    # 3. Detenemos monitorización
    stop_monitor.set()

    # 4. Limpiamos procesos
    for p in procs:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        except:
            pass

    t.join()

    # 5. Cálculos y Resultados
    if stats:
        # Extraemos la media de los picos totales de consumo
        avg_total_cpu = statistics.mean([c for m, c in stats])
        avg_total_ram = statistics.mean([m for m, c in stats])

        # Calculamos la media real por nodo basándonos en el total
        real_avg_cpu_per_node = avg_total_cpu / num_nodes
        real_avg_ram_per_node = avg_total_ram / num_nodes

        print(f"   -> Consumo TOTAL del sistema: CPU {avg_total_cpu:.1f}% | RAM {avg_total_ram:.1f} MB")
        print(f"   -> Consumo medio por nodo:    CPU {real_avg_cpu_per_node:.1f}% | RAM {real_avg_ram_per_node:.1f} MB")
    else:
        print("   -> Error: No se pudieron recolectar métricas.")

def main():
    print("=========================================================")
    print(" MIDDLEWARE DDS EXHAUSTIVE BENCHMARK ")
    print("=========================================================")

    # Definimos los vectores de pruebas (De menos a más exigente)
    payload_sizes = [1024, 10240, 100000, 500000, 1048576, 5242880, 10485760] # 1KB a 10MB
    late_joiner_delays = [1, 3, 5, 10, 20] # Segundos
    chaos_profiles = [(0, 5), (5, 20), (10, 50), (20, 100), (30, 200), (50, 500)] # (Pérdida %, Latencia ms)
    scalability_nodes = [2, 5, 10, 20, 30, 50] # Número de publicadores simultáneos

    for mw_name, config in MIDDLEWARES.items():
        print(f"\n\n{'#'*60}")
        print(f"  EVALUANDO MIDDLEWARE: {mw_name.upper()}")
        print(f"{'#'*60}")

        # 1. Batería de pruebas Throughput
        print("\n[BATERÍA 1] Evaluando Ancho de Banda y Carga de CPU...")
        for size in payload_sizes:
            test_1_throughput(mw_name, config, payload_size=size)

        # 2. Batería de pruebas Late Joiner
        print("\n[BATERÍA 2] Evaluando Memoria Histórica (Late Joiner)...")
        for delay in late_joiner_delays:
            test_2_qos_late_joiner(mw_name, config, delay_seconds=delay)

        # 3. Batería de pruebas Caos de Red (Aparcada temporalmente)
        # print("\n[BATERÍA 3] Evaluando Resiliencia de Red...")
        # for loss, delay in chaos_profiles:
        #     test_3_network_chaos(mw_name, config, loss_pct=loss, delay_ms=delay)

        # 4. Batería de pruebas Escalabilidad
        print("\n[BATERÍA 4] Evaluando Escalabilidad y Descubrimiento...")
        for nodes in scalability_nodes:
            test_4_scalability(mw_name, config, num_nodes=nodes)

        print(f"\n>>> Fin de pruebas exhaustivas para {mw_name}. Limpiando red...")
        time.sleep(3)

if __name__ == "__main__":
    main()
