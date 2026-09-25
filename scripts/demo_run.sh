#!/bin/bash
# demo_run.sh — DTLCP 一键演示：FULL / RESUME / PSK 三模式对比
# 用法：bash scripts/demo_run.sh   （Windows Git Bash / Linux）
set -e
cd "$(dirname "$0")/.."
# 平台产物后缀（MinGW 需 .exe，Linux/macOS 无后缀）
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE="" ;;
esac
PORT=${PORT:-9011}
SESS_GW=bin/sess_gw.bin
SESS_SN=bin/sess_sn.bin
LOG=bin/demo_scenes.log

bash scripts/build.sh >/dev/null
rm -f $SESS_GW $SESS_SN
: > $LOG

echo "=============================================================="
echo " DTLCP Demo - Lightweight SM Crypto Transport (sensor <-> gateway)"
echo "=============================================================="

echo ""
echo "--- Scenario 1/3: FULL (ECDHE + SM2 mutual auth, 1.5-RTT) ---"
./bin/dtlcp_gateway$EXE --mode full --port $PORT --msgs 3 --save-session $SESS_GW &
GW=$!
sleep 0.5
./bin/dtlcp_sensor$EXE --mode full --port $PORT --msgs 3 --save-session $SESS_SN | tee -a $LOG
wait $GW

echo ""
echo "--- Scenario 2/3: RESUME (session resumption, 0 SM2 ops) ---"
./bin/dtlcp_gateway$EXE --mode resume --port $PORT --msgs 3 --load-session $SESS_GW &
GW=$!
sleep 0.5
./bin/dtlcp_sensor$EXE --mode resume --port $PORT --msgs 3 --load-session $SESS_SN | tee -a $LOG
wait $GW

echo ""
echo "--- Scenario 3/3: PSK (pre-shared key, 0-RTT early data) ---"
./bin/dtlcp_gateway$EXE --mode psk --port $PORT --msgs 4 &
GW=$!
sleep 0.5
./bin/dtlcp_sensor$EXE --mode psk --port $PORT --msgs 3 --early "boot-telemetry:ok" | tee -a $LOG
wait $GW

echo ""
echo "=============================================================="
echo " Summary (details in docs/04)                                  "
echo "=============================================================="
echo "  mode     flights      SM2 ops            auth            early-data"
echo "  FULL     3 (1.5-RTT)  sign+verify+ECDH   mutual cert     no"
echo "  RESUME   2 (1-RTT)    0                  session cache   no"
echo "  PSK      1 (0-RTT)    0                  pre-shared key  yes"
echo "=============================================================="
echo "  完整 sensor 输出：bin/demo_scenes.log"
