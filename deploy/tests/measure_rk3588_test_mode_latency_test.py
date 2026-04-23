import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from measure_rk3588_test_mode_latency import compute_metrics


class MeasureRk3588TestModeLatencyTest(unittest.TestCase):
    def test_computes_stage_latencies_from_marker_events(self):
        metrics = compute_metrics(
            video_events=[{"event": "playback_started", "ts_ms": 1000}],
            fall_events=[
                {"event": "first_analysis_frame", "ts_ms": 1120},
                {"event": "first_classification", "ts_ms": 1240, "state": "monitoring"},
            ],
        )

        self.assertEqual(metrics["playback_start_ts_ms"], 1000)
        self.assertEqual(metrics["first_frame_ts_ms"], 1120)
        self.assertEqual(metrics["first_classification_ts_ms"], 1240)
        self.assertEqual(metrics["analysis_ingress_latency_ms"], 120)
        self.assertEqual(metrics["classification_stage_latency_ms"], 120)
        self.assertEqual(metrics["startup_classification_latency_ms"], 240)
        self.assertEqual(metrics["first_state"], "monitoring")


if __name__ == "__main__":
    unittest.main()
