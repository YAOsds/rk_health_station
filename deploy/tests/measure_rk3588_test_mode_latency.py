#!/usr/bin/env python3
import argparse
import json
import os
import pathlib
import shlex
import subprocess
import tempfile
import time


def compute_metrics(video_events, fall_events):
    playback = next(event for event in video_events if event["event"] == "playback_started")
    first_frame = next(event for event in fall_events if event["event"] == "first_analysis_frame")
    first_classification = next(
        event for event in fall_events if event["event"] == "first_classification"
    )
    return {
        "playback_start_ts_ms": playback["ts_ms"],
        "first_frame_ts_ms": first_frame["ts_ms"],
        "first_classification_ts_ms": first_classification["ts_ms"],
        "analysis_ingress_latency_ms": first_frame["ts_ms"] - playback["ts_ms"],
        "classification_stage_latency_ms": first_classification["ts_ms"] - first_frame["ts_ms"],
        "startup_classification_latency_ms": first_classification["ts_ms"] - playback["ts_ms"],
        "first_state": first_classification.get("state", ""),
    }


def build_askpass_script(password):
    script = tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8")
    script.write("#!/bin/sh\n")
    script.write("echo %s\n" % shlex.quote(password))
    script.close()
    os.chmod(script.name, 0o700)
    return script.name


def run_ssh(host, password, remote_cmd):
    askpass = build_askpass_script(password)
    env = os.environ.copy()
    env["SSH_ASKPASS"] = askpass
    env["SSH_ASKPASS_REQUIRE"] = "force"
    env["DISPLAY"] = env.get("DISPLAY", "codex:0")
    try:
        return subprocess.run(
            [
                "setsid",
                "ssh",
                "-o",
                "StrictHostKeyChecking=no",
                "-o",
                "NumberOfPasswordPrompts=1",
                host,
                remote_cmd,
            ],
            check=True,
            capture_output=True,
            text=True,
            env=env,
        )
    finally:
        pathlib.Path(askpass).unlink(missing_ok=True)


def fetch_remote_events(host, password, video_marker, fall_marker):
    remote_cmd = (
        "python3 - <<'PY'\n"
        "import json\n"
        "import pathlib\n"
        f"video_path = pathlib.Path({video_marker!r})\n"
        f"fall_path = pathlib.Path({fall_marker!r})\n"
        "def load(path):\n"
        "    if not path.exists():\n"
        "        return []\n"
        "    events = []\n"
        "    for line in path.read_text(encoding='utf-8').splitlines():\n"
        "        line = line.strip()\n"
        "        if line:\n"
        "            events.append(json.loads(line))\n"
        "    return events\n"
        "print(json.dumps({'video_events': load(video_path), 'fall_events': load(fall_path)}))\n"
        "PY"
    )
    result = run_ssh(host, password, remote_cmd)
    return json.loads(result.stdout.strip() or "{}")


def start_bundle(host, password, bundle_dir, video_marker, fall_marker):
    remote_cmd = (
        f"cd {shlex.quote(bundle_dir)} && "
        "./scripts/stop.sh >/dev/null 2>&1 || true && "
        f"rm -f {shlex.quote(video_marker)} {shlex.quote(fall_marker)} && "
        f"export RK_VIDEO_LATENCY_MARKER_PATH={shlex.quote(video_marker)} && "
        f"export RK_FALL_LATENCY_MARKER_PATH={shlex.quote(fall_marker)} && "
        "RK_RUNTIME_MODE=system ./scripts/start.sh --backend-only"
    )
    run_ssh(host, password, remote_cmd)


def stop_bundle(host, password, bundle_dir):
    remote_cmd = f"cd {shlex.quote(bundle_dir)} && ./scripts/stop.sh >/dev/null 2>&1 || true"
    run_ssh(host, password, remote_cmd)


def trigger_test_input(host, password, bundle_dir, video_file):
    remote_cmd = (
        "python3 - <<'PY'\n"
        "import json\n"
        "import socket\n"
        f"sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
        f"sock.connect({str(pathlib.PurePosixPath(bundle_dir) / 'run' / 'rk_video.sock')!r})\n"
        "command = {\n"
        "    'action': 'start_test_input',\n"
        "    'request_id': 'latency-1',\n"
        "    'camera_id': 'front_cam',\n"
        f"    'payload': {{'file_path': {video_file!r}}},\n"
        "}\n"
        "sock.sendall((json.dumps(command) + '\\n').encode('utf-8'))\n"
        "print(sock.recv(65535).decode('utf-8'))\n"
        "PY"
    )
    run_ssh(host, password, remote_cmd)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--bundle-dir", required=True)
    parser.add_argument("--video-file", default="/home/elf/Videos/video.mp4")
    parser.add_argument("--video-marker", default="/tmp/rk_video_latency.jsonl")
    parser.add_argument("--fall-marker", default="/tmp/rk_fall_latency.jsonl")
    parser.add_argument("--timeout-seconds", type=float, default=10.0)
    args = parser.parse_args()

    start_bundle(args.host, args.password, args.bundle_dir, args.video_marker, args.fall_marker)
    try:
        trigger_test_input(args.host, args.password, args.bundle_dir, args.video_file)
        deadline = time.monotonic() + args.timeout_seconds
        while time.monotonic() < deadline:
            events = fetch_remote_events(args.host, args.password, args.video_marker, args.fall_marker)
            video_events = events.get("video_events", [])
            fall_events = events.get("fall_events", [])
            if any(event.get("event") == "first_classification" for event in fall_events):
                print(json.dumps(compute_metrics(video_events, fall_events), indent=2, sort_keys=True))
                return
            time.sleep(0.2)
        raise SystemExit("timed out waiting for first_classification marker")
    finally:
        stop_bundle(args.host, args.password, args.bundle_dir)


if __name__ == "__main__":
    main()
