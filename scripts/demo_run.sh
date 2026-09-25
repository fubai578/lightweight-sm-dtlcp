#!/bin/bash
# demo_run.sh — DTLCP 一键演示：FULL / RESUME / PSK 三模式对比
# 用法：bash scripts/demo_run.sh   （Windows Git Bash / Linux）
set -e
cd "$(dirname "$0")/.."
PORT=${PORT:-9011}
SESS_GW=bin/sess_gw.bin
SESS_SN=bin/sess_sn.bin

bash scripts/build.sh >/dev/null
rm -f $SESS_GW $SESS_SN

echo "=============================================================="
echo " DTLCP Demo - Lightweight SM Crypto Transport (sensor <-> gateway)"
echo "=============================================================="

LOG=bin/demo_scenes.log
: > $LOG

echo ""
echo "--- Scenario 1/3: FULL (ECDHE + SM2 mutual auth, 1.5-RTT) ---"
./bin/dtlcp_gateway.exe --mode full --port $PORT --msgs 3 --save-session $SESS_GW &
GW=$!
sleep 0.5
./bin/dtlcp_sensor.exe --mode full --port $PORT --msgs 3 --save-session $SESS_SN | tee -a $LOG
wait $GW

echo ""
echo "--- Scenario 2/3: RESUME (session resumption, 0 SM2 ops) ---"
./bin/dtlcp_gateway.exe --mode resume --port $PORT --msgs 3 --load-session $SESS_GW &
GW=$!
sleep 0.5
./bin/dtlcp_sensor.exe --mode resume --port $PORT --msgs 3 --load-session $SESS_SN | tee -a $LOG
wait $GW

echo ""
echo "--- Scenario 3/3: PSK (pre-shared key, 0-RTT early data) ---"
./bin/dtlcp_gateway.exe --mode psk --port $PORT --msgs 4 &   # 4 = 1 early + 3 telemetry
GW=$!
sleep 0.5
./bin/dtlcp_sensor.exe --mode psk --port $PORT --msgs 3 --early "boot-telemetry:ok" | tee -a $LOG
wait $GW

echo ""
echo "=============================================================="
echo " Summary (details in docs/04)"
echo "=============================================================="
echo "  mode     flights    SM2 ops        auth              early-data"
echo "  FULL     3 (1.5-RTT) sign+verify+ECDH  mutual cert      no"
echo "  RESUME   2 (1-RTT)   0                session cache    no"
echo "  PSK      1 (0-RTT)   0                pre-shared key   yes"
echo "=============================================================="

echo ""
echo "---- 实测对比表（sensor 端） ----"
printf "%-8s %-14s %-12s %-10s %-14s
" "mode" "handshake" "SM2 ops" "wire(B)" "app RTT avg"
grep -E "handshake flights|handshake cost|握手 flights|握手开销" $LOG | head -6
echo "(完整输出：bin/demo_scenes.log)"
