"""Regression coverage for automatic coast-calibration math and lifecycle guards."""
from pathlib import Path
import math
import unittest

ROOT = Path(__file__).resolve().parents[2]
MIN_RATIO, MAX_RATIO, SMOOTHING = 0.25, 1.5, 0.20

def next_ratio(current, stop_weight, settled_weight, latency_ms, flow_gps):
    observed_coast = settled_weight - stop_weight
    predicted_coast = latency_ms / 1000.0 * flow_gps
    if (not all(math.isfinite(v) for v in (observed_coast, predicted_coast)) or observed_coast < 0 or predicted_coast <= 0): return None
    observed_ratio = observed_coast / predicted_coast
    if not math.isfinite(observed_ratio): return None
    bounded = max(MIN_RATIO, min(MAX_RATIO, observed_ratio))
    return max(MIN_RATIO, min(MAX_RATIO, current + (bounded - current) * SMOOTHING))

class AutoCoastCalibrationTest(unittest.TestCase):
    def test_bounded_smoothing_and_invalid_observations(self):
        self.assertAlmostEqual(next_ratio(1.0, 10.0, 10.2, 100, 1.0), 1.1)
        self.assertAlmostEqual(next_ratio(1.0, 10.0, 20.0, 100, 1.0), 1.1)
        self.assertAlmostEqual(next_ratio(1.4, 10.0, 10.001, 100, 1.0), 1.17)
        for values in ((10, 9.9, 100, 1), (10, 10.2, 0, 1), (10, 10.2, 100, 0), (10, math.nan, 100, 1)): self.assertIsNone(next_ratio(1.0, *values))

    def test_controller_lifecycle_guards_and_main_stop_boundary(self):
        controller = (ROOT / 'src/controllers/grind_controller.cpp').read_text()
        strategy = (ROOT / 'src/controllers/weight_grind_strategy.cpp').read_text()
        for guard in ('coast_ratio_for_session_ = coast_ratio_;', 'last_session_result_ != GrindSessionResult::SUCCESS', 'pulse_attempts != 0', 'mechanical_anomaly_count_ != 0', '!weight_sensor->noise_level_diagnostic()'): self.assertIn(guard, controller)
        api = (ROOT / 'src/network/device_api.cpp').read_text()
        page = (ROOT / 'src/network/device_page.h').read_text()
        self.assertIn('auto_coast_calibration', api)
        self.assertIn('save_auto_coast_calibration', api)
        self.assertIn('min="0.25" max="1.5"', page)
        self.assertIn('ratio.disabled=automatic', page)
        self.assertIn('record_predictive_stop(', strategy)
        self.assertIn('if (!controller.main_stop_settled_)', strategy)
        self.assertLess(strategy.index('record_main_stop_settled_weight('), strategy.index('switch_phase(GrindPhase::PULSE_DECISION'))

if __name__ == '__main__': unittest.main()
