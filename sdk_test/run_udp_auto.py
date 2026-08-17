#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Automated driver for `sdk_standalone_test.exe --udp`.

The test program has 4 interactive "stop/restart the UDP simulator, then press Enter"
prompts to exercise the async-timeout path. When stdin is a pipe, getchar() would
return EOF immediately and the simulator would never actually be stopped -- so every
"timeout" command would get ACKed and the timeout checks would spuriously FAIL.

Why we DON'T stop/restart the simulator process:
  On Windows, when two UDP sockets bind 0.0.0.0:<port> with SO_REUSEADDR, the
  FIRST-bound socket owns all traffic; later binds are shadowed and receive nothing.
  The SDK's own data socket binds 0.0.0.0:60000 (data port == target port), so a
  simulator restarted AFTER the SDK device opens would be shadowed and never see a
  command. Hence this driver keeps ONE simulator alive (bound before the SDK) and
  toggles its ACK behaviour via a dedicated admin UDP port (dataPort+1):
      "PAUSE"  -> simulator stops replying (simulates FPGA no-response)
      "RESUME" -> simulator resumes replying

This driver:
  * runs ONE simulator as a subprocess (bound BEFORE the SDK device opens)
  * runs the test exe as a subprocess (piped stdin/stdout)
  * pauses / resumes ACK at the exact prompts and feeds Enter
  * watches the simulator log (flushed in real-time) for deterministic stage markers
"""

import os
import socket
import subprocess
import time
import threading
import sys
import re

SIM        = "python"
SIM_SCRIPT = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/sdk_test/udp_simulator.py"
SIM_ARGS   = ["60000", "5000"]
DATA_PORT  = int(SIM_ARGS[0])
ADMIN_PORT = DATA_PORT + 1   # simulator's admin port = data_port + 1
LOG        = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/sdk_test/udp_simulator.log"
LOG_SAVE   = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/sdk_test/udp_sim_phase{}.log"
TEST       = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/out/build/x64-Debug/sdk_standalone_test.exe"

_sim_phase = 0


def save_log():
    """Snapshot the sim log (grows cumulatively; there is no process restart now)."""
    global _sim_phase
    _sim_phase += 1
    try:
        with open(LOG, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()
        with open(LOG_SAVE.format(_sim_phase), 'w', encoding='utf-8') as f:
            f.write(content)
        logp(f"[driver] saved phase {_sim_phase} log ({len(content)} chars)")
    except FileNotFoundError:
        logp(f"[driver] no log to save for phase {_sim_phase}")

out_lines = []
print_lock = threading.Lock()


def logp(msg):
    with print_lock:
        print(msg, flush=True)


def kill_stale_sims():
    """Kill any leftover udp_simulator.py processes so they can't ACK commands."""
    import subprocess as sp
    try:
        out = sp.check_output(
            ["wmic", "process", "where", "name='python.exe'", "get", "ProcessId,CommandLine"],
            stderr=sp.DEVNULL).decode('utf-8', errors='replace')
    except Exception as e:
        logp(f"[driver] wmic probe failed: {e}")
        return
    for line in out.splitlines():
        if "udp_simulator.py" in line:
            parts = line.split()
            if parts:
                pid = parts[-1]
                try:
                    int(pid)
                except ValueError:
                    continue
                logp(f"[driver] killing stale simulator PID {pid}")
                sp.run(["taskkill", "/F", "/PID", pid],
                       stdout=sp.DEVNULL, stderr=sp.DEVNULL)


def read_log():
    try:
        with open(LOG, 'r', encoding='utf-8') as f:
            return f.read()
    except FileNotFoundError:
        return ""


def sim_ready():
    return "Listening on" in read_log()


def start_sim():
    # 删除旧日志：start_sim 用 "Listening on" 判断新模拟器就绪。
    # 若旧日志残留 "Listening on"，会误判为新进程已就绪，导致提前进入
    # 下一阶段（过早 PAUSE），从而破坏同步命令。删除后 "Listening on"
    # 只可能来自新启动的模拟器。
    try:
        os.remove(LOG)
    except FileNotFoundError:
        pass
    logp("[driver] starting simulator ...")
    p = subprocess.Popen([SIM, SIM_SCRIPT] + SIM_ARGS,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 15
    while time.time() < deadline:
        if sim_ready():
            logp("[driver] simulator ready (bound)")
            return p
        time.sleep(0.2)
    raise RuntimeError("simulator did not become ready")


def stop_sim(p):
    logp("[driver] stopping simulator ...")
    if p is None:
        return None
    p.terminate()
    try:
        p.wait(timeout=5)
    except Exception:
        p.kill()
    logp("[driver] simulator stopped")
    return None


def admin(cmd: str):
    """Send a control word to the simulator's admin UDP port (data_port+1)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.sendto(cmd.encode('utf-8'), ("127.0.0.1", ADMIN_PORT))
    finally:
        s.close()
    logp(f"[driver] admin: {cmd}")


def pause_ack():
    admin("PAUSE")


def resume_ack():
    admin("RESUME")


def wait_for(pred, timeout=90, what=""):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if pred():
            return True
        time.sleep(0.2)
    logp(f"[driver] TIMEOUT waiting for: {what}")
    return False


def enter(test):
    test.stdin.write("\n")
    test.stdin.flush()
    logp("[driver] sent Enter")


def reader(test):
    try:
        for line in iter(test.stdout.readline, ''):
            with print_lock:
                out_lines.append(line)
    finally:
        with print_lock:
            out_lines.append(None)


def main():
    kill_stale_sims()
    time.sleep(0.5)
    sim = start_sim()
    resume_ack()  # ensure ACK is enabled from the start

    test = subprocess.Popen([TEST, "--udp"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding='utf-8', errors='replace', bufsize=1)
    threading.Thread(target=reader, args=(test,), daemon=True).start()

    try:
        # ── Stage A: normal + async tests ──────────────────────────────────
        # All capture/batch ops now go through CMD_BATCH_CONTROL (0x0204).
        # SENSOR1 定向的 BATCH_CONTROL 共 8 条（最后一条为 async StopCapture）：
        #   StartCapture(1)=1, StopCapture(1)=2,
        #   StartCapture(0 广播→s1)=3, StopCapture(0 广播→s1)=4,
        #   StartBatch(1)=5, StopBatch(1)=6,
        #   AsyncStartCapture(1)=7, AsyncStopCapture(1)=8
        def bc_count():
            return len(re.findall(r"WRITE BATCH_CONTROL\s*Dev=SENSOR1", read_log()))

        ok = wait_for(lambda: bc_count() >= 8, what="8th BATCH_CONTROL(s1) (async StopCapture)")
        if not ok:
            return 2
        logp(f"[driver] Stage A done: BATCH_CONTROL(s1)={bc_count()}")
        time.sleep(0.5)  # let the async StopCapture ACK land

        # ── Prompt 1: "stop the simulator", Enter ──────────────────────────
        # Pause ACK to simulate FPGA no-response. The test now runs two
        # sequential asyncs: StopCapture (1 subcmd, 1000ms) + SetSensorConfig
        # (4 subcmds, 1000ms each) ≈ 5-6s. Use 10s margin.
        pause_ack()
        save_log()
        enter(test)
        time.sleep(10)

        # ── Prompt 2: "restart the simulator", Enter ───────────────────────
        # Resume ACK BEFORE feeding Enter, so the recovery SetSensorConfig
        # writes actually get ACKed (the test checks result == CAM_OK).
        resume_ack()
        time.sleep(0.3)  # let the admin RESUME land before the test retries
        enter(test)

        # ── Recovery test: SetSensorConfig writes ... SAMPLE_RATE=10 (last) ─
        # The last recovery write is SAMPLE_RATE=10 → payload hex 0000000a.
        def recovery_seen():
            return "Payload hex: 0000000a" in read_log()
        ok = wait_for(recovery_seen, what="recovery SAMPLE_RATE=10 write")
        if not ok:
            return 2
        logp("[driver] recovery write seen")
        time.sleep(1.0)  # let all recovery ACKs + wait_async complete

        # ── Prompt 3: "stop the simulator", Enter ──────────────────────────
        pause_ack()
        save_log()
        enter(test)
        time.sleep(8)  # three concurrent 1000ms-timeout asyncs (~1.5-2.5s)

        # ── Prompt 4: "restart the simulator", Enter ───────────────────────
        resume_ack()
        time.sleep(0.3)
        enter(test)

        # ── Wrap-up: CloseDevice / Finalize ────────────────────────────────
        try:
            rc = test.wait(timeout=45)
        except subprocess.TimeoutExpired:
            logp("[driver] TEST STILL RUNNING after 45s (hang?) -- killing")
            test.kill()
            rc = test.wait(timeout=5)
            rc = rc if rc is not None else -999
    finally:
        time.sleep(1)
        # If the test is still running (early return), close stdin so getchar()
        # returns EOF; the test then finishes and flushes its buffered stdout.
        if test.poll() is None:
            try:
                test.stdin.close()
            except Exception:
                pass
            try:
                test.wait(timeout=20)
            except subprocess.TimeoutExpired:
                logp("[driver] test did not exit after EOF -- killing")
                test.kill()
                test.wait(timeout=5)
        sim = stop_sim(sim)
        sys.stdout.write("".join(line for line in out_lines if line is not None))
        sys.stdout.flush()

    logp(f"[driver] === test exit code = {rc} ===")
    return rc


if __name__ == "__main__":
    sys.exit(main())
