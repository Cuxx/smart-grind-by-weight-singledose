import sys
import time
import requests

host = sys.argv[1] if len(sys.argv) > 1 else "10.0.0.121"
bin_path = sys.argv[2] if len(sys.argv) > 2 else "/home/cmossom/src/smart-grind-by-weight/firmware_cache/build_001.bin"

print(f"Preparing OTA on {host}...")
r = requests.post(f"http://{host}/api/v1/ota/prepare", timeout=10)
print(f"Prepare response ({r.status_code}): {r.text}")

print("Waiting for OTA ready...")
for i in range(40):
    time.sleep(0.3)
    try:
        s = requests.get(f"http://{host}/api/v1/status", timeout=5).json()
        if s.get("ota", {}).get("ready"):
            print("Grinder is ready for OTA upload!")
            break
        print(f"Waiting... ota status: {s.get('ota')}")
    except Exception as e:
        print(f"Polling: {e}")
else:
    print("Grinder did not report ready in time!")
    sys.exit(1)

print(f"Uploading firmware binary ({bin_path})...")
with open(bin_path, "rb") as f:
    files = {"firmware": ("smart-grind-by-weight.bin", f, "application/octet-stream")}
    r = requests.post(f"http://{host}/api/v1/ota", files=files, timeout=90)
    print(f"Upload result ({r.status_code}): {r.text}")

print("Done! Grinder will restart.")
