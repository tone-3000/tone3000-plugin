#!/usr/bin/env bash
# On-device USB audio scenarios for the probe (plan M2.3/M2.4, L3).
#
#   ./script/usb-scenarios.sh doctor
#   ./script/usb-scenarios.sh start [packets urbs ring rate]
#   ./script/usb-scenarios.sh status
#   ./script/usb-scenarios.sh rate 96000 [packets urbs ring]
#   ./script/usb-scenarios.sh discard
#   ./script/usb-scenarios.sh rtl [packets urbs ring rate]
#   ./script/usb-scenarios.sh matrix
#   ./script/usb-scenarios.sh soak 60 [--screen-off]
#
# Defaults are the M2.4 shipping layout measured on the AMS-22: 8 packets
# per URB (1 ms), 4 URBs in flight, 128-frame capture ring, 48 kHz. 8x3 is
# 1.2 ms faster but its two-URB playback slack is too small: OUT URBs miss
# the slot often enough to overrun the ring. 8x4 holds zero overruns.
#
# adb: everything is driven through the probe service so the screen can be
# off. The first command goes through the activity (it owns the USB
# permission prompt); later commands use `am startservice`, which Android
# 16 only allows while the probe is a foreground service.
set -euo pipefail
cd "$(dirname "$0")/.."

export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
export PATH="$ANDROID_HOME/platform-tools:$PATH"

PKG=com.TONE3000.USBCHECK
ACT=.MainActivity
SVC=.ProbeService
RESULTS=plugin/docs/usb-audio/results-m2-soak.txt

PACKETS=${PACKETS:-8}
URBS=${URBS:-4}
RING=${RING:-128}
RATE=${RATE:-48000}

if [ -z "${ANDROID_SERIAL:-}" ]; then
    ANDROID_SERIAL=$(adb devices | awk '/\tdevice$/{print $1; exit}')
fi
[ -n "$ANDROID_SERIAL" ] || { echo "no adb device (set ANDROID_SERIAL)" >&2; exit 1; }
# Every adb call is bounded. A stale Wi-Fi transport used to hang the soak
# logger forever (the stream itself kept running); a timeout plus one
# reconnect keeps the CSV going.
ADB_TIMEOUT=${ADB_TIMEOUT:-30}
adb() { timeout "$ADB_TIMEOUT" env adb -s "$ANDROID_SERIAL" "$@"; }

reconnect() {
    adb disconnect "$ANDROID_SERIAL" >/dev/null 2>&1 || true
    sleep 1
    adb connect "$ANDROID_SERIAL" >/dev/null 2>&1 || true
    sleep 2
}

send_service() {
    adb shell am startservice -n "$PKG/$SVC" "$@" >/dev/null
}

send_activity() {
    adb shell am start -n "$PKG/$ACT" "$@" >/dev/null
}

status_line() {
    local attempt line
    for attempt in 1 2 3; do
        send_service -e mode status >/dev/null 2>&1 || true
        sleep 0.7
        line=$(adb shell run-as "$PKG" tail -1 files/probe.log 2>/dev/null | tr -d '\r' || true)
        case "$line" in
            *"status: running="*) echo "$line"; return 0 ;;
        esac
        if [ "$attempt" = 2 ]; then
            reconnect || true
            send_service -e mode status >/dev/null 2>&1 || true
            sleep 0.7
        fi
        sleep 0.4
    done
    echo "status: unavailable"
    return 1
}

wait_running() {
    for _ in $(seq 1 20); do
        line=$(status_line || true)
        case "$line" in
            *"status: running=1"*) echo "$line"; return 0 ;;
        esac
        sleep 0.5
    done
    echo "stream did not start: $line" >&2
    return 1
}

cmd_doctor() {
    adb devices -l
    echo "--- usb topology ---"
    adb shell 'for d in /sys/bus/usb/devices/*; do
        [ -e "$d/idVendor" ] || continue
        echo "$(basename $d) $(cat $d/idVendor):$(cat $d/idProduct) $(cat $d/product 2>/dev/null) speed=$(cat $d/speed 2>/dev/null)"
    done'
    echo "--- battery ---"
    adb shell 'dumpsys battery | grep -E "level:|status:|powered:|current now"'
    echo "--- power ---"
    adb shell 'dumpsys power | grep -m1 mWakefulness='
    echo "--- process ---"
    adb shell "pidof $PKG || echo 'not running'"
}

cmd_start() {
    PACKETS=${1:-$PACKETS}; URBS=${2:-$URBS}; RING=${3:-$RING}; RATE=${4:-$RATE}
    send_activity -e mode stream --ei rate "$RATE" --ei packets "$PACKETS" \
        --ei urbs "$URBS" --ei ring "$RING"
    wait_running
}

cmd_rate() {
    local hz=$1; shift || true
    PACKETS=${1:-$PACKETS}; URBS=${2:-$URBS}; RING=${3:-$RING}
    send_service -e mode rate --ei rate "$hz" --ei packets "$PACKETS" \
        --ei urbs "$URBS" --ei ring "$RING"
    sleep 1
    adb shell run-as "$PKG" tail -2 files/probe.log | tr -d '\r'
}

cmd_matrix() {
    local pair p u
    for pair in "8 2" "8 3" "8 4" "4 4" "4 3" "2 3"; do
        set -- $pair; p=$1; u=$2
        send_activity -e mode stream --ei rate 48000 --ei packets "$p" \
            --ei urbs "$u" --ei ring 128
        wait_running >/dev/null || { sleep 1; continue; }
        sleep 1
        a=$(status_line); t0=$(date +%s)
        sleep 8
        b=$(status_line); t1=$(date +%s)
        echo "[$p x $u] start: $a"
        echo "[$p x $u] end:   $b  (${t1}s - ${t0}s)"
        send_service -e mode stop
        sleep 1
    done
}

cmd_soak() {
    local minutes=${1:-60}
    shift || true
    local screen_off=false
    [ "${1:-}" = "--screen-off" ] && screen_off=true

    mkdir -p "$(dirname "$RESULTS")"
    if [ ! -s "$RESULTS" ]; then
        echo "# M2.4 L3 soak (script/usb-scenarios.sh)" > "$RESULTS"
        echo "# layout=$PACKETS x $URBS, ring=$RING, rate=$RATE, screen_off=$screen_off" >> "$RESULTS"
        echo "timestamp,elapsed_s,capUrb,playUrb,capPkts,playPkts,xruns,ringOver,ringUnder,submitFail,fbErr,fbVal,drift,cap_pkts_per_s,play_pkts_per_s" >> "$RESULTS"
    fi

    cmd_start >/dev/null
    if $screen_off; then
        adb shell input keyevent 26
        sleep 2
    fi

    local prev cur t0 t1 dt
    local p_cap p_play p_capk p_playk
    local c_cap c_play c_capk c_playk c_xr c_over c_under c_sf c_fberr c_fbval c_drift
    prev=$(status_line); t0=$(date +%s)
    p_cap=$(get_field "$prev" capUrb); p_play=$(get_field "$prev" playUrb)
    p_capk=$(get_field "$prev" capPkts); p_playk=$(get_field "$prev" playPkts)
    local i
    for i in $(seq 1 "$minutes"); do
        sleep 60
        # Notifications can wake the panel; keep it off for the soak.
        if $screen_off; then
            wake=$(adb shell 'dumpsys power | grep -m1 mWakefulness=' || true)
            case "$wake" in *Awake*) adb shell input keyevent 26 ;; esac
        fi
        cur=$(status_line || true); t1=$(date +%s); dt=$((t1 - t0))
        [ "$dt" -gt 0 ] || dt=1
        case "$cur" in
            *"status: running="*) ;;
            *) echo "$(date -Iseconds) gap: $cur" >> "$RESULTS"
               t0=$t1; continue ;;
        esac
        c_cap=$(get_field "$cur" capUrb); c_play=$(get_field "$cur" playUrb)
        c_capk=$(get_field "$cur" capPkts); c_playk=$(get_field "$cur" playPkts)
        c_xr=$(get_field "$cur" xruns); c_over=$(get_field "$cur" ringOver)
        c_under=$(get_field "$cur" ringUnder); c_sf=$(get_field "$cur" submitFail)
        c_fberr=$(get_field "$cur" fbErr); c_fbval=$(get_field "$cur" fbVal)
        c_drift=$(get_field "$cur" drift)
        echo "$(date -Iseconds),$dt,$c_cap,$c_play,$c_capk,$c_playk,$c_xr,$c_over,$c_under,$c_sf,$c_fberr,$c_fbval,$c_drift,$(( (c_capk - p_capk) / dt )),$(( (c_playk - p_playk) / dt ))" >> "$RESULTS"
        printf '%s elapsed=%ss capUrb=%s playUrb=%s xruns=%s ringOver=%s ringUnder=%s fbErr=%s fbVal=%s drift=%s\n' \
            "$(date +%H:%M:%S)" "$dt" "$c_cap" "$c_play" "$c_xr" "$c_over" "$c_under" "$c_fberr" "$c_fbval" "$c_drift"
        t0=$t1
        p_cap=$c_cap; p_play=$c_play; p_capk=$c_capk; p_playk=$c_playk
    done

    if $screen_off; then
        adb shell input keyevent 26 || true
    fi
    send_service -e mode stop
    sleep 1
    echo "soak complete: $cur"
}

get_field() {
    local line=$1 key=$2
    echo "$line" | sed -n "s/.*[ ]$key=\([^ ]*\).*/\1/p"
}

case "${1:-}" in
    doctor) cmd_doctor ;;
    start) shift; cmd_start "$@" ;;
    stop) send_service -e mode stop; sleep 1; adb shell run-as "$PKG" tail -1 files/probe.log ;;
    status) status_line ;;
    rate) shift; cmd_rate "$@" ;;
    discard) send_service -e mode discard; sleep 1; status_line ;;
    rtl) shift; \
        send_service -e mode rtl --ei rate "${4:-48000}" --ei packets "${1:-8}" \
            --ei urbs "${2:-4}" --ei ring "${3:-128}"; sleep 6; \
        adb shell run-as "$PKG" tail -1 files/probe.log ;;
    matrix) cmd_matrix ;;
    soak) shift; cmd_soak "$@" ;;
    *) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac
