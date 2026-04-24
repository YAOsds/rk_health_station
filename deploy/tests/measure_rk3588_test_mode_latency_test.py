import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from measure_rk3588_test_mode_latency import compare_runs, compute_metrics


class MeasureRk3588TestModeLatencyTest(unittest.TestCase):
    def test_computes_stage_latencies_from_marker_events(self):
        metrics = compute_metrics(
            video_events=[
                {"event": "playback_started", "ts_ms": 1000},
                {"event": "analysis_descriptor_published", "ts_ms": 1080, "dropped_frames": 2},
            ],
            fall_events=[
                {"event": "analysis_descriptor_ingested", "ts_ms": 1095},
                {"event": "first_analysis_frame", "ts_ms": 1120},
                {"event": "first_classification", "ts_ms": 1240, "state": "monitoring"},
            ],
            system_metrics={"producer_cpu_pct": 22.0, "consumer_cpu_pct": 18.0},
        )

        self.assertEqual(metrics["playback_start_ts_ms"], 1000)
        self.assertEqual(metrics["analysis_publish_ts_ms"], 1080)
        self.assertEqual(metrics["analysis_ingest_ts_ms"], 1095)
        self.assertEqual(metrics["first_frame_ts_ms"], 1120)
        self.assertEqual(metrics["first_classification_ts_ms"], 1240)
        self.assertEqual(metrics["transport_latency_ms"], 15)
        self.assertEqual(metrics["analysis_ingress_latency_ms"], 120)
        self.assertEqual(metrics["classification_stage_latency_ms"], 120)
        self.assertEqual(metrics["startup_classification_latency_ms"], 240)
        self.assertEqual(metrics["producer_dropped_frames"], 2)
        self.assertEqual(metrics["producer_cpu_pct"], 22.0)
        self.assertEqual(metrics["consumer_cpu_pct"], 18.0)
        self.assertEqual(metrics["first_state"], "monitoring")

    def test_compare_runs_reports_transport_improvement(self):
        comparison = compare_runs(
            {
                "transport_latency_ms": 40,
                "producer_cpu_pct": 22.0,
                "consumer_cpu_pct": 18.0,
                "producer_dropped_frames": 5,
            },
            {
                "transport_latency_ms": 18,
                "producer_cpu_pct": 17.0,
                "consumer_cpu_pct": 15.0,
                "producer_dropped_frames": 1,
            },
        )

        self.assertEqual(comparison["transport_latency_delta_ms"], -22)
        self.assertEqual(comparison["producer_cpu_delta_pct"], -5.0)
        self.assertEqual(comparison["consumer_cpu_delta_pct"], -3.0)
        self.assertEqual(comparison["producer_drop_delta"], -4)

    def test_compare_runs_tolerates_missing_transport_metric(self):
        comparison = compare_runs(
            {
                "transport_latency_ms": None,
                "producer_cpu_pct": 22.0,
                "consumer_cpu_pct": 18.0,
                "producer_dropped_frames": 0,
            },
            {
                "transport_latency_ms": 18,
                "producer_cpu_pct": 17.0,
                "consumer_cpu_pct": 15.0,
                "producer_dropped_frames": 1,
            },
        )

        self.assertIsNone(comparison["transport_latency_delta_ms"])
        self.assertEqual(comparison["producer_cpu_delta_pct"], -5.0)
        self.assertEqual(comparison["consumer_cpu_delta_pct"], -3.0)
        self.assertEqual(comparison["producer_drop_delta"], 1)

    def test_computes_metrics_when_marker_numbers_are_strings(self):
        metrics = compute_metrics(
            video_events=[
                {"event": "playback_started", "ts_ms": 1000},
                {"event": "analysis_descriptor_published", "ts_ms": 1010, "dropped_frames": "3"},
            ],
            fall_events=[
                {"event": "analysis_descriptor_ingested", "ts_ms": 1015},
                {"event": "first_analysis_frame", "ts_ms": 1020},
                {"event": "first_classification", "ts_ms": 1050, "state": "monitoring"},
            ],
        )

        self.assertEqual(metrics["producer_dropped_frames"], 3)


if __name__ == "__main__":
    unittest.main()
