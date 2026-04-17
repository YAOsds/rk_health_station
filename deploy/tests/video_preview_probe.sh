#!/usr/bin/env bash
set -euo pipefail

DEVICE=${1:-/dev/video11}
OUT_DIR=${2:-/tmp/video_preview_probe}
mkdir -p "${OUT_DIR}"

run_case() {
  local name=$1
  local launch=$2
  local log="${OUT_DIR}/${name}.log"
  echo "=== ${name} ===" | tee "${log}"
  bash -lc "timeout 8 ${launch}" >>"${log}" 2>&1 || true
  if grep -q "ERROR\\|Failed\\|not-negotiated" "${log}"; then
    echo "${name}: FAIL" | tee -a "${log}"
  else
    echo "${name}: PASS" | tee -a "${log}"
  fi
}

run_e2e_case() {
  local name=$1
  local producer_launch=$2
  local consumer_launch=$3
  local log="${OUT_DIR}/${name}.log"
  local producer_log="${OUT_DIR}/${name}_producer.log"
  local consumer_log="${OUT_DIR}/${name}_consumer.log"

  echo "=== ${name} ===" | tee "${log}"
  bash -lc "${producer_launch}" >"${producer_log}" 2>&1 &
  local producer_pid=$!
  sleep 2

  bash -lc "timeout 5 ${consumer_launch}" >"${consumer_log}" 2>&1 || true

  kill "${producer_pid}" >/dev/null 2>&1 || true
  wait "${producer_pid}" 2>/dev/null || true

  cat "${producer_log}" >>"${log}"
  printf '\n--- CONSUMER ---\n' >>"${log}"
  cat "${consumer_log}" >>"${log}"

  if grep -q "ERROR\\|Failed\\|not-negotiated" "${producer_log}" "${consumer_log}"; then
    echo "${name}: FAIL" | tee -a "${log}"
  else
    echo "${name}: PASS" | tee -a "${log}"
  fi
}

run_e2e_case udp_mpegts_h264 \
  "gst-launch-1.0 -e v4l2src device=${DEVICE} ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! mpph264enc ! h264parse ! mpegtsmux ! udpsink host=127.0.0.1 port=5602 sync=false async=false" \
  "gst-launch-1.0 -v udpsrc port=5602 caps='video/mpegts,systemstream=(boolean)true,packetsize=(int)188' ! tsdemux ! h264parse ! mppvideodec ! fakesink sync=false"

run_case udp_h264 \
  "gst-launch-1.0 -e v4l2src device=${DEVICE} ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! mpph264enc ! h264parse ! rtph264pay pt=96 config-interval=1 ! udpsink host=127.0.0.1 port=5600"

run_case tcp_mjpeg \
  "gst-launch-1.0 -e v4l2src device=${DEVICE} ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! jpegenc ! multipartmux boundary=spionisto ! tcpserversink host=127.0.0.1 port=5601"

printf 'probe logs written to %s\n' "${OUT_DIR}"
