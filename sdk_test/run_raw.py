import subprocess, time, sys, os, threading

SIM = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/sdk_test/udp_simulator.py"
TEST = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/out/build/x64-Debug/sdk_standalone_test.exe"
LOG = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/sdk_test/udp_simulator.log"
OUT = r"D:/CODE2/FPGA_TEST/fpga_upper_cpp/sdk_test/udp_raw_out.txt"

try: os.remove(LOG)
except: pass

sim = subprocess.Popen(["python", SIM, "60000", "5000"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(75):
    if os.path.exists(LOG) and "Listening on" in open(LOG, encoding='utf-8', errors='replace').read():
        break
    time.sleep(0.2)

test = subprocess.Popen([TEST, "--udp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=0)

lines = []
def reader():
    try:
        for ln in test.stdout:
            lines.append(ln)
    except Exception:
        pass
t = threading.Thread(target=reader, daemon=True); t.start()

time.sleep(20)
# close stdin -> EOF so interactive prompts proceed and buffered output flushes
try: test.stdin.close()
except: pass
try:
    test.wait(timeout=30)
except subprocess.TimeoutExpired:
    test.kill(); test.wait(timeout=5)
time.sleep(1)

with open(OUT, 'w', encoding='utf-8') as f:
    f.write("".join(lines))
sim.terminate()
try: sim.wait(timeout=5)
except: sim.kill()
print("written", OUT, "len", len("".join(lines)))