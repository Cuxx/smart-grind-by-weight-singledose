import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class WindowDeltaNoiseTest(unittest.TestCase):
    def test_window_delta_spike_rejection(self):
        circular_cpp = (ROOT / "src/hardware/circular_buffer_math/circular_buffer_math.cpp").read_text(encoding="utf-8")
        self.assertIn("median3", circular_cpp)
        self.assertIn("filtered_newest = median3(", circular_cpp)
        self.assertIn("filtered_oldest = median3(", circular_cpp)

        timeout_cpp = (ROOT / "src/ui/controllers/screen_timeout_controller.cpp").read_text(encoding="utf-8")
        self.assertIn("[DISPLAY] Weight wake activity:", timeout_cpp)
        self.assertIn("[DISPLAY] Wake from %s (source: %s%s%s", timeout_cpp)

        cpp_code = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>

static uint32_t fake_millis = 10000;
uint32_t millis() { return fake_millis; }

struct AdcSample {
    int32_t raw_value;
    uint32_t timestamp_ms;
};

class FakeCircularBufferMath {
public:
    static const uint16_t MAX_BUFFER_SIZE = 1024;
    AdcSample circular_buffer[MAX_BUFFER_SIZE];
    uint16_t write_index = 0;
    uint16_t samples_count = 0;

    void add_sample(int32_t raw_value, uint32_t timestamp_ms) {
        circular_buffer[write_index].raw_value = raw_value;
        circular_buffer[write_index].timestamp_ms = timestamp_ms;
        write_index = (write_index + 1) % MAX_BUFFER_SIZE;
        if (samples_count < MAX_BUFFER_SIZE) {
            samples_count++;
        }
    }

    bool get_window_delta(uint32_t window_ms, int32_t* delta_out,
                          uint32_t* span_ms_out = nullptr, int* samples_out = nullptr) const {
        if (!delta_out || samples_count < 2) {
            if (delta_out) *delta_out = 0;
            return false;
        }

        uint32_t current_time = millis();
        uint32_t window_start = current_time - window_ms;

        int collected = 0;
        int32_t newest_raw = 0;
        int32_t oldest_raw = 0;
        uint32_t newest_ts = 0;
        uint32_t oldest_ts = 0;

        for (int i = 0; i < samples_count; ++i) {
            uint16_t index = (write_index - 1 - i + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
            const AdcSample& sample = circular_buffer[index];

            if (sample.timestamp_ms < window_start) {
                break;
            }

            if (collected == 0) {
                newest_raw = sample.raw_value;
                newest_ts = sample.timestamp_ms;
            }

            oldest_raw = sample.raw_value;
            oldest_ts = sample.timestamp_ms;
            ++collected;
        }

        if (samples_out) *samples_out = collected;

        if (collected < 2) {
            *delta_out = 0;
            if (span_ms_out) *span_ms_out = 0;
            return false;
        }

        int32_t filtered_newest = newest_raw;
        int32_t filtered_oldest = oldest_raw;

        if (collected >= 6) {
            auto median3 = [](int32_t a, int32_t b, int32_t c) -> int32_t {
                if (a > b) {
                    if (b > c) return b;
                    return (a > c) ? c : a;
                } else {
                    if (a > c) return a;
                    return (b > c) ? c : b;
                }
            };

            const uint16_t idx0 = (write_index - 1 + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
            const uint16_t idx1 = (write_index - 2 + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
            const uint16_t idx2 = (write_index - 3 + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;

            const uint16_t idx_old0 = (write_index - 1 - (collected - 1) + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
            const uint16_t idx_old1 = (write_index - 1 - (collected - 2) + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;
            const uint16_t idx_old2 = (write_index - 1 - (collected - 3) + MAX_BUFFER_SIZE) % MAX_BUFFER_SIZE;

            filtered_newest = median3(circular_buffer[idx0].raw_value,
                                      circular_buffer[idx1].raw_value,
                                      circular_buffer[idx2].raw_value);

            filtered_oldest = median3(circular_buffer[idx_old0].raw_value,
                                      circular_buffer[idx_old1].raw_value,
                                      circular_buffer[idx_old2].raw_value);
        }

        *delta_out = filtered_newest - filtered_oldest;

        if (span_ms_out) {
            if (newest_ts >= oldest_ts) {
                *span_ms_out = newest_ts - oldest_ts;
            } else {
                *span_ms_out = (UINT32_MAX - oldest_ts) + newest_ts + 1;
            }
        }

        return true;
    }
};

int main() {
    FakeCircularBufferMath buf;

    // Populate with 20 stable samples at 10000 counts, spaced by 50ms (1000ms window)
    for (int i = 0; i < 20; ++i) {
        buf.add_sample(10000, 9050 + i * 50);
    }
    fake_millis = 10000;

    int32_t delta = 0;
    int samples = 0;
    assert(buf.get_window_delta(1000, &delta, nullptr, &samples));
    assert(samples == 20);
    assert(delta == 0);

    // Test 1: Single spike at newest sample (e.g. +2000 counts ADC glitch)
    fake_millis = 10050;
    buf.add_sample(12000, fake_millis);
    assert(buf.get_window_delta(1000, &delta, nullptr, &samples));
    // Without median filtering, delta would be 2000! With median filtering, it must be 0!
    assert(delta == 0);

    // Test 2: Next sample returns to baseline (10000)
    fake_millis = 10100;
    buf.add_sample(10000, fake_millis);
    assert(buf.get_window_delta(1000, &delta, nullptr, &samples));
    assert(delta == 0);

    // Test 3: Real sustained weight step (+5000 counts, e.g. placing cup)
    fake_millis = 10150;
    buf.add_sample(15000, fake_millis);
    fake_millis = 10200;
    buf.add_sample(15000, fake_millis);
    fake_millis = 10250;
    buf.add_sample(15000, fake_millis);
    assert(buf.get_window_delta(1000, &delta, nullptr, &samples));
    // Sustained step is detected
    assert(delta == 5000);

    // Test 4: Single spike at oldest boundary
    FakeCircularBufferMath buf2;
    buf2.add_sample(13000, 9050); // Spike at oldest sample
    for (int i = 1; i < 20; ++i) {
        buf2.add_sample(10000, 9050 + i * 50);
    }
    fake_millis = 10000;
    assert(buf2.get_window_delta(1000, &delta, nullptr, &samples));
    // Without median filtering, delta would be -3000! With median filtering, it must be 0!
    assert(delta == 0);

    printf("All window delta spike rejection tests passed!\n");
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            cpp, binary = Path(tmp) / "window_delta_test.cpp", Path(tmp) / "window_delta_test"
            cpp.write_text(cpp_code)
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined", str(cpp), "-o", str(binary)],
                check=True,
            )
            res = subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE, text=True, timeout=15)
            self.assertIn("All window delta spike rejection tests passed!", res.stdout)


if __name__ == "__main__":
    unittest.main()
