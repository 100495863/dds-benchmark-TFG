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
# --- DEFINICIÓN DE MIDDLEWARES ---
MIDDLEWARES = {
    "OpenDDS": {
        "cwd": os.path.join(HOME, "DDS/DDS/benchmark/OpenDDS/dron"),
        "shell": True,
        # OpenDDS usa 'exec' para reemplazar la shell, heredando el buffer. 
        # Podemos intentar añadir stdbuf aquí también si fallara, pero parece ir bien.
        "flight_control_cmd": f"source {OPENDDS_ROOT}/setenv.sh && exec stdbuf -oL ./flight_controller -DCPSConfigFile rtps.ini",
        "sensor_imu_cmd": f"source {OPENDDS_ROOT}/setenv.sh && exec ./sensor_imu -DCPSConfigFile rtps.ini",
        "sensor_env_cmd": f"source {OPENDDS_ROOT}/setenv.sh && exec ./sensor_env -DCPSConfigFile rtps.ini",
    },
    "FastDDS": {
        "cwd": os.path.join(HOME, "DDS/DDS/benchmark/FastDDS/dron/build"),
        "shell": False,
        # AÑADIDO stdbuf -oL
        "flight_control_cmd": ["stdbuf", "-oL", "./flight_controller"],
        "sensor_imu_cmd": ["./sensor_imu"],
        "sensor_env_cmd": ["./sensor_env"],
    },
    "CycloneDDS": {
        "cwd": os.path.join(HOME, "DDS/DDS/benchmark/CycloneDDS/dron/build"),
        "shell": False,
        # AÑADIDO stdbuf -oL AQUÍ ES DONDE FALLABA
        "flight_control_cmd": ["stdbuf", "-oL", "./flight_controller"],
        "sensor_imu_cmd": ["stdbuf", "-oL", "./sensor_imu"],
        "sensor_env_cmd": ["stdbuf", "-oL", "./sensor_env"],
    }
}


TEST_DURATION = 15

def get_process_stats(pid):
    """Obtiene uso de RAM (MB) y CPU (%) de un PID y sus hijos."""
    total_mem = 0.0
    total_cpu = 0.0
    pids_to_check = [pid]

    try:
        children = subprocess.run(
            ["pgrep", "-P", str(pid)],
            capture_output=True, text=True
        ).stdout.strip().split()
        for child in children:
            pids_to_check.append(int(child))
    except Exception:
        pass

    for p in pids_to_check:
        try:
            res = subprocess.run(
                ["ps", "-p", str(p), "-o", "rss=,%cpu="],
                capture_output=True, text=True
            )
            if res.returncode == 0 and res.stdout.strip():
                parts = res.stdout.strip().split()
                if len(parts) >= 2:
                    total_mem += float(parts[0]) / 1024.0
                    total_cpu += float(parts[1])
        except Exception:
            pass
    return total_mem, total_cpu

def monitor_thread_func(pid, stop_event, stats_list):
    """Hilo de monitoreo de recursos."""
    while not stop_event.is_set():
        mem, cpu = get_process_stats(pid)
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

    flight_control_proc = None
    sensor_imu_proc = None
    sensor_env_proc = None
    
    stop_monitors = threading.Event()
    flight_control_stats = []
    sensor_imu_stats = []
    sensor_env_stats = []
    
    monitors = []

    # 1. ARRANCAR SUBSCRIPTOR (Controller)
    print(f" -> [SUB] Iniciando flight_controller...")
    cmd_sub = config['flight_control_cmd']
    if config['shell'] and isinstance(cmd_sub, str):
        cmd_sub = f"bash -c '{cmd_sub}'"

    try:
        flight_control_proc = subprocess.Popen(
            cmd_sub, cwd=config['cwd'], shell=config['shell'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, preexec_fn=os.setsid
        )
        t = threading.Thread(target=monitor_thread_func, args=(flight_control_proc.pid, stop_monitors, flight_control_stats))
        t.start()
        monitors.append(t)
    except Exception as e:
        print(f"Error flight_controller: {e}")
        return

    print(" -> Esperando descubrimiento (3s)...")
    time.sleep(3)

    # 2. ARRANCAR PUBLICADORES
    for name, cmd, stats_list in [
        ("sensor_imu", config['sensor_imu_cmd'], sensor_imu_stats),
        ("sensor_env", config['sensor_env_cmd'], sensor_env_stats)
    ]:
        print(f" -> [PUB] Iniciando {name}...")
        c = cmd
        if config['shell'] and isinstance(c, str):
            c = f"bash -c '{c}'"
        try:
            proc = subprocess.Popen(
                c, cwd=config['cwd'], shell=config['shell'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=os.setsid
            )
            if name == "sensor_imu": sensor_imu_proc = proc
            else: sensor_env_proc = proc
            
            t = threading.Thread(target=monitor_thread_func, args=(proc.pid, stop_monitors, stats_list))
            t.start()
            monitors.append(t)
        except Exception as e:
            print(f"Error {name}: {e}")

    # 3. RECOLECTAR DATOS (PARSEO DE LOGS)
    print(" -> Recibiendo datos ", end="", flush=True)
    
    # Contadores
    count_turbulence = 0
    count_system = 0
    count_fcc_logs = 0
    raw_lines = 0

    start_time = time.time()
    
    try:
        if flight_control_proc:
            # Leemos línea a línea
            while (time.time() - start_time) < TEST_DURATION:
                # readline puede bloquear, usamos un pequeño truco o confiamos en que hay flujo constante
                line = flight_control_proc.stdout.readline()
                if not line:
                    break
                
                raw_lines += 1
                
                # --- LÓGICA DE DETECCIÓN ---
                if "[ALERTA]" in line:
                    count_turbulence += 1
                    print("!", end="", flush=True) # Feedback visual de turbulencia
                elif "[SISTEMA]" in line:
                    count_system += 1
                    print("s", end="", flush=True) # Feedback visual de sistema
                elif "[FCC]" in line:
                    count_fcc_logs += 1
                    print(".", end="", flush=True) # Feedback visual de heartbeat
                elif "[ALARMA]" in line:
                    print("X", end="", flush=True) # Batería crítica

    except KeyboardInterrupt:
        print("\nInterrumpido.")
    
    finally:
        print("\n -> Finalizando procesos...")
        stop_monitors.set()
        
        # Matar procesos
        for p in [flight_control_proc, sensor_imu_proc, sensor_env_proc]:
            if p:
                try: os.killpg(os.getpgid(p.pid), signal.SIGTERM)
                except: pass
        
        # Asegurar limpieza
        subprocess.run(["pkill", "-f", "flight_controller"], stderr=subprocess.DEVNULL)
        subprocess.run(["pkill", "-f", "sensor_imu"], stderr=subprocess.DEVNULL)
        subprocess.run(["pkill", "-f", "sensor_env"], stderr=subprocess.DEVNULL)

        for t in monitors:
            t.join()

    # 4. RESULTADOS
    # Calcular promedios de recursos
    def avg(lst): return statistics.mean([x for x, y in lst]) if lst else 0
    def avg_cpu(lst): return statistics.mean([y for x, y in lst]) if lst else 0

    print(f"\n RESULTADOS {mw_name}:")
    print(f" ------------------------------------------------")
    print(f"  EVENTOS DETECTADOS:")
    print(f"    - Turbulencias ([ALERTA]): {count_turbulence}")
    print(f"    - Status Sistema ([SISTEMA]): {count_system}")
    print(f"    - Logs Control ([FCC]):       {count_fcc_logs}")
    print(f"    - Total Líneas Log:           {raw_lines}")
    print(f" ------------------------------------------------")
    print(f"  RECURSOS (Promedio durante {TEST_DURATION}s):")
    print(f"    - FLIGHT CPU: {avg_cpu(flight_control_stats):5.2f}% | RAM: {avg(flight_control_stats):5.2f} MB")
    print(f"    - IMU    CPU: {avg_cpu(sensor_imu_stats):5.2f}% | RAM: {avg(sensor_imu_stats):5.2f} MB")
    print(f"    - ENV    CPU: {avg_cpu(sensor_env_stats):5.2f}% | RAM: {avg(sensor_env_stats):5.2f} MB")
    print(f" ------------------------------------------------")

def main():
    # Ejecutar OpenDDS
    run_benchmark("OpenDDS", MIDDLEWARES["OpenDDS"])
    time.sleep(3)
    # Ejecutar FastDDS
    run_benchmark("FastDDS", MIDDLEWARES["FastDDS"])
    time.sleep(3)
    # Ejecutar CycloneDDS (si lo tienes compilado)
    run_benchmark("CycloneDDS", MIDDLEWARES["CycloneDDS"])

if __name__ == "__main__":
    main()
