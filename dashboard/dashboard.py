"""
dashboard.py  —  Adaptive Disaster Mesh Network Dashboard
Adaptive Wireless Mesh Communication System v2.0

Reads structured JSON from Node D (LoRa gateway) over USB Serial.
Falls back to simulation mode when no device is connected.

Run:
    pip install streamlit pyserial pandas plotly
    streamlit run dashboard.py

Simulation mode (no hardware):
    streamlit run dashboard.py -- --simulate
"""

import sys
import json
import time
import random
import argparse
import threading
from collections import deque
from datetime import datetime, timedelta

import pandas as pd
import plotly.graph_objects as go
import plotly.express as px
import streamlit as st

# ── Try importing serial (optional for simulation mode) ───────
try:
    import serial
    import serial.tools.list_ports
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

# ─────────────────────────────────────────────────────────────
#  Configuration
# ─────────────────────────────────────────────────────────────

SERIAL_BAUD        = 115200
SERIAL_DATA_PREFIX = "DATA:"
MAX_ALERTS         = 200          # Rolling window for alert log
MAX_RSSI_POINTS    = 60           # Last 60 RSSI readings on chart
NODE_OFFLINE_SEC   = 15           # Node marked offline after N seconds
REFRESH_INTERVAL   = 2            # Dashboard refresh interval (seconds)
NODES              = ["A", "B", "C", "D"]

SEVERITY_COLORS = {
    "CRITICAL": "#E24B4A",
    "HIGH":     "#BA7517",
    "MEDIUM":   "#1D9E75",
    "LOW":      "#378ADD",
    "UNKNOWN":  "#888780",
}

SENSOR_ICONS = {
    "VIBRATION": "🌍",
    "GAS":       "💨",
    "FLOOD":     "🌊",
    "UNKNOWN":   "⚠️",
}

# ─────────────────────────────────────────────────────────────
#  Shared state (thread-safe via lock)
# ─────────────────────────────────────────────────────────────

class MeshState:
    def __init__(self):
        self.lock       = threading.Lock()
        self.alerts     = deque(maxlen=MAX_ALERTS)
        self.rssi_log   = deque(maxlen=MAX_RSSI_POINTS)
        self.node_last_seen   = {n: None  for n in NODES}
        self.node_tx_ok       = {n: 0     for n in NODES}
        self.node_tx_fail     = {n: 0     for n in NODES}
        self.node_uptime      = {n: 0     for n in NODES}
        self.node_vib_events  = {n: 0     for n in NODES}
        self.node_gas_alerts  = {n: 0     for n in NODES}
        self.node_flood_alerts= {n: 0     for n in NODES}
        self.node_gas_now     = {n: 0     for n in NODES}
        self.node_flood_now   = {n: 0     for n in NODES}
        self.total_packets    = 0
        self.parse_errors     = 0
        self.connected        = False
        self.port_name        = "—"

mesh = MeshState()

# ─────────────────────────────────────────────────────────────
#  Packet processor
# ─────────────────────────────────────────────────────────────

def process_packet(raw: str):
    """Parse a JSON packet and update shared state."""
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        with mesh.lock:
            mesh.parse_errors += 1
        return

    node     = data.get("node", "?")
    msg_type = data.get("type", "?")
    now      = datetime.now()

    with mesh.lock:
        mesh.total_packets += 1

        # Update last-seen for this node
        if node in NODES:
            mesh.node_last_seen[node] = now

        if msg_type == "HEARTBEAT":
            if node in NODES:
                mesh.node_tx_ok[node]        = data.get("tx_ok",   0)
                mesh.node_tx_fail[node]      = data.get("tx_fail", 0)
                mesh.node_uptime[node]       = data.get("uptime_ms", 0)
                mesh.node_vib_events[node]   = data.get("vib_events", 0)
                mesh.node_gas_alerts[node]   = data.get("gas_alerts", 0)
                mesh.node_flood_alerts[node] = data.get("flood_alerts", 0)
                mesh.node_gas_now[node]      = data.get("gas_now", 0)
                mesh.node_flood_now[node]    = data.get("flood_now", 0)

            # Track RSSI from gateway packets
            if "rssi" in data:
                mesh.rssi_log.append({
                    "time": now,
                    "rssi": data["rssi"],
                    "snr":  data.get("snr", 0),
                    "node": node,
                })

        elif msg_type == "ALERT":
            mesh.alerts.appendleft({
                "time":     now,
                "node":     node,
                "sensor":   data.get("sensor",   "UNKNOWN"),
                "severity": data.get("severity", "UNKNOWN"),
                "value":    data.get("value",    0),
                "desc":     data.get("desc",     ""),
                "rssi":     data.get("rssi",     None),
            })

        elif msg_type == "STATUS":
            offline_node = data.get("offline_node", "?")
            silent_ms    = data.get("silent_ms", 0)
            mesh.alerts.appendleft({
                "time":     now,
                "node":     "GATEWAY",
                "sensor":   "NETWORK",
                "severity": "HIGH",
                "value":    0,
                "desc":     f"Node {offline_node} offline — silent for {silent_ms/1000:.0f}s",
                "rssi":     None,
            })

# ─────────────────────────────────────────────────────────────
#  Serial reader thread
# ─────────────────────────────────────────────────────────────

def serial_reader(port: str):
    """Runs in background thread — reads serial and processes packets."""
    while True:
        try:
            with serial.Serial(port, SERIAL_BAUD, timeout=1) as ser:
                with mesh.lock:
                    mesh.connected = True
                    mesh.port_name = port
                while True:
                    line = ser.readline().decode("utf-8", errors="replace").strip()
                    if line.startswith(SERIAL_DATA_PREFIX):
                        process_packet(line[len(SERIAL_DATA_PREFIX):])
        except Exception as e:
            with mesh.lock:
                mesh.connected = False
            time.sleep(3)

# ─────────────────────────────────────────────────────────────
#  Simulator thread (no hardware needed)
# ─────────────────────────────────────────────────────────────

def simulator():
    """Generates realistic fake packets for demo / testing."""
    boot_time = time.time()
    tx_counters = {n: 0 for n in NODES}
    alert_types = [
        ("VIBRATION", "HIGH",     1.0,  "Seismic vibration detected at Node C"),
        ("GAS",       "CRITICAL", 3200, "Gas concentration elevated — ADC=3200"),
        ("GAS",       "HIGH",     2600, "Gas concentration elevated — ADC=2600"),
        ("FLOOD",     "HIGH",     2800, "Water level critical — ADC=2800"),
    ]

    with mesh.lock:
        mesh.connected = True
        mesh.port_name = "SIMULATOR"

    while True:
        now_ms = int((time.time() - boot_time) * 1000)

        # Heartbeats from active nodes (A and C)
        for node in ["A", "C"]:
            tx_counters[node] += 1
            hb = {
                "node":         node,
                "type":         "HEARTBEAT",
                "ts":           now_ms,
                "ver":          "2.0.0",
                "uptime_ms":    now_ms,
                "tx_ok":        tx_counters[node],
                "tx_fail":      random.randint(0, 2),
                "vib_events":   random.randint(0, 5),
                "gas_alerts":   random.randint(0, 3),
                "flood_alerts": random.randint(0, 2),
                "gas_now":      random.randint(400, 800),
                "flood_now":    random.randint(200, 600),
                "rssi":         random.randint(-95, -60),
                "snr":          round(random.uniform(3.0, 9.5), 1),
            }
            process_packet(json.dumps(hb))

        # Random alert (30% chance per cycle)
        if random.random() < 0.30:
            sensor, severity, value, desc = random.choice(alert_types)
            alert = {
                "node":     "C",
                "type":     "ALERT",
                "ts":       now_ms,
                "ver":      "2.0.0",
                "sensor":   sensor,
                "severity": severity,
                "value":    value,
                "desc":     desc,
                "rssi":     random.randint(-105, -65),
                "snr":      round(random.uniform(2.0, 8.0), 1),
            }
            process_packet(json.dumps(alert))

        time.sleep(HEARTBEAT_INTERVAL := 5)

# ─────────────────────────────────────────────────────────────
#  Dashboard UI
# ─────────────────────────────────────────────────────────────

def node_status_color(node: str) -> str:
    with mesh.lock:
        last = mesh.node_last_seen[node]
    if last is None:
        return "#888780"  # Gray — never seen
    elapsed = (datetime.now() - last).total_seconds()
    if elapsed < NODE_OFFLINE_SEC:
        return "#1D9E75"  # Green — online
    return "#E24B4A"      # Red — offline


def node_status_text(node: str) -> str:
    with mesh.lock:
        last = mesh.node_last_seen[node]
    if last is None:
        return "UNKNOWN"
    elapsed = (datetime.now() - last).total_seconds()
    return "ONLINE" if elapsed < NODE_OFFLINE_SEC else f"OFFLINE ({elapsed:.0f}s)"


def render_dashboard():
    st.set_page_config(
        page_title="Disaster Mesh Dashboard",
        page_icon="🛰️",
        layout="wide",
        initial_sidebar_state="collapsed",
    )

    # ── CSS ──────────────────────────────────────────────────
    st.markdown("""
    <style>
      .node-card {
        border-radius: 10px; padding: 14px 16px; margin-bottom: 10px;
        border: 1px solid rgba(0,0,0,0.08);
        background: white;
      }
      .node-title { font-size: 18px; font-weight: 600; margin-bottom: 2px; }
      .node-meta  { font-size: 12px; color: #888; margin-bottom: 8px; }
      .stat-label { font-size: 11px; color: #aaa; }
      .stat-value { font-size: 15px; font-weight: 500; }
      .alert-critical { border-left: 4px solid #E24B4A; padding: 6px 10px; margin: 4px 0; border-radius: 0 6px 6px 0; background: #fff5f5; }
      .alert-high     { border-left: 4px solid #BA7517; padding: 6px 10px; margin: 4px 0; border-radius: 0 6px 6px 0; background: #fffbf0; }
      .alert-medium   { border-left: 4px solid #1D9E75; padding: 6px 10px; margin: 4px 0; border-radius: 0 6px 6px 0; background: #f0faf6; }
      .alert-low      { border-left: 4px solid #378ADD; padding: 6px 10px; margin: 4px 0; border-radius: 0 6px 6px 0; background: #f0f6ff; }
    </style>
    """, unsafe_allow_html=True)

    # ── Header ───────────────────────────────────────────────
    with mesh.lock:
        connected   = mesh.connected
        port_name   = mesh.port_name
        total_pkts  = mesh.total_packets
        parse_errs  = mesh.parse_errors
        alert_count = len(mesh.alerts)

    col_title, col_status = st.columns([3, 1])
    with col_title:
        st.title("🛰️ Adaptive Disaster Mesh Network")
        st.caption("Real-time monitoring dashboard — ESP-NOW + LoRa Mesh")
    with col_status:
        st.metric("Connection", port_name)
        status_dot = "🟢" if connected else "🔴"
        st.markdown(f"{status_dot} {'Connected' if connected else 'Disconnected'}")

    st.divider()

    # ── Top metrics ──────────────────────────────────────────
    m1, m2, m3, m4 = st.columns(4)
    m1.metric("Total Packets", f"{total_pkts:,}")
    m2.metric("Active Alerts", alert_count)
    m3.metric("Parse Errors",  parse_errs)

    with mesh.lock:
        online_count = sum(
            1 for n in NODES
            if mesh.node_last_seen[n] is not None and
               (datetime.now() - mesh.node_last_seen[n]).total_seconds() < NODE_OFFLINE_SEC
        )
    m4.metric("Nodes Online", f"{online_count} / {len(NODES)}")

    st.divider()

    # ── Node status grid ─────────────────────────────────────
    st.subheader("Node Health")
    node_cols = st.columns(len(NODES))

    for i, node in enumerate(NODES):
        with node_cols[i]:
            color  = node_status_color(node)
            status = node_status_text(node)

            with mesh.lock:
                uptime_ms  = mesh.node_uptime[node]
                tx_ok      = mesh.node_tx_ok[node]
                tx_fail    = mesh.node_tx_fail[node]
                vib        = mesh.node_vib_events[node]
                gas_a      = mesh.node_gas_alerts[node]
                flood_a    = mesh.node_flood_alerts[node]

            uptime_str = str(timedelta(seconds=uptime_ms // 1000)) if uptime_ms else "—"
            tx_total   = tx_ok + tx_fail
            success_rt = f"{(tx_ok/tx_total*100):.0f}%" if tx_total > 0 else "—"

            st.markdown(f"""
            <div class="node-card" style="border-top: 3px solid {color};">
              <div class="node-title">Node {node}</div>
              <div class="node-meta" style="color:{color}; font-weight:500;">{status}</div>
              <div style="display:grid; grid-template-columns:1fr 1fr; gap:6px; margin-top:8px;">
                <div><div class="stat-label">Uptime</div><div class="stat-value">{uptime_str}</div></div>
                <div><div class="stat-label">TX success</div><div class="stat-value">{success_rt}</div></div>
                <div><div class="stat-label">Vibrations</div><div class="stat-value">{vib}</div></div>
                <div><div class="stat-label">Gas alerts</div><div class="stat-value">{gas_a}</div></div>
              </div>
            </div>
            """, unsafe_allow_html=True)

    st.divider()

    # ── Two-column layout: alerts + RSSI chart ───────────────
    col_alerts, col_chart = st.columns([1, 1])

    with col_alerts:
        st.subheader("Alert Log")

        with mesh.lock:
            alerts_snapshot = list(mesh.alerts)

        if not alerts_snapshot:
            st.info("No alerts received yet — system nominal.")
        else:
            for a in alerts_snapshot[:30]:  # Show latest 30
                severity = a["severity"]
                cls      = f"alert-{severity.lower()}"
                icon     = SENSOR_ICONS.get(a["sensor"], "⚠️")
                ts       = a["time"].strftime("%H:%M:%S")
                rssi_str = f" | RSSI {a['rssi']} dBm" if a["rssi"] else ""

                st.markdown(f"""
                <div class="{cls}">
                  <strong>{icon} {a['sensor']}</strong>
                  <span style="float:right; font-size:11px; color:#aaa;">{ts}{rssi_str}</span><br>
                  <span style="font-size:12px; color:#555;">Node {a['node']} · {severity} · value={a['value']}</span><br>
                  <span style="font-size:11px; color:#888;">{a['desc']}</span>
                </div>
                """, unsafe_allow_html=True)

    with col_chart:
        st.subheader("Signal Strength (RSSI)")

        with mesh.lock:
            rssi_snapshot = list(mesh.rssi_log)

        if len(rssi_snapshot) < 2:
            st.info("Collecting signal data...")
        else:
            df = pd.DataFrame(rssi_snapshot)
            fig = go.Figure()
            fig.add_trace(go.Scatter(
                x=df["time"], y=df["rssi"],
                mode="lines+markers",
                name="RSSI (dBm)",
                line=dict(color="#378ADD", width=2),
                marker=dict(size=4),
            ))
            # Reference lines
            fig.add_hline(y=-100, line_dash="dash", line_color="#E24B4A",
                          annotation_text="Weak signal", annotation_position="bottom right")
            fig.add_hline(y=-80, line_dash="dash", line_color="#BA7517",
                          annotation_text="Acceptable", annotation_position="bottom right")

            fig.update_layout(
                xaxis_title="Time",
                yaxis_title="RSSI (dBm)",
                yaxis=dict(range=[-130, -40]),
                height=320,
                margin=dict(l=40, r=20, t=20, b=40),
                plot_bgcolor="white",
                paper_bgcolor="white",
            )
            st.plotly_chart(fig, use_container_width=True)

        # SNR gauge
        with mesh.lock:
            last_snr = mesh.rssi_log[-1]["snr"] if mesh.rssi_log else None
        if last_snr is not None:
            fig2 = go.Figure(go.Indicator(
                mode="gauge+number",
                value=last_snr,
                title={"text": "SNR (dB)"},
                gauge={
                    "axis": {"range": [-5, 15]},
                    "bar":  {"color": "#378ADD"},
                    "steps": [
                        {"range": [-5, 3],  "color": "#FCEBEB"},
                        {"range": [3,  8],  "color": "#FAEEDA"},
                        {"range": [8,  15], "color": "#E1F5EE"},
                    ],
                },
            ))
            fig2.update_layout(height=220, margin=dict(l=20, r=20, t=40, b=20))
            st.plotly_chart(fig2, use_container_width=True)

    st.divider()

    # ── Sensor readings bar chart ─────────────────────────────
    st.subheader("Live Sensor Readings — Node C")
    with mesh.lock:
        gas_now   = mesh.node_gas_now["C"]
        flood_now = mesh.node_flood_now["C"]

    if gas_now > 0 or flood_now > 0:
        fig3 = go.Figure()
        fig3.add_trace(go.Bar(
            x=["Gas (ADC)", "Flood (ADC)"],
            y=[gas_now, flood_now],
            marker_color=["#BA7517" if gas_now > 2000 else "#1D9E75",
                          "#E24B4A" if flood_now > 1800 else "#1D9E75"],
            text=[f"{gas_now}", f"{flood_now}"],
            textposition="outside",
        ))
        # Threshold lines
        fig3.add_hline(y=2000, line_dash="dot", line_color="#BA7517",
                       annotation_text="Gas threshold")
        fig3.add_hline(y=1800, line_dash="dot", line_color="#E24B4A",
                       annotation_text="Flood threshold")
        fig3.update_layout(
            yaxis=dict(range=[0, 4095], title="ADC Units (0–4095)"),
            height=250,
            margin=dict(l=40, r=20, t=20, b=40),
            plot_bgcolor="white",
            paper_bgcolor="white",
        )
        st.plotly_chart(fig3, use_container_width=True)
    else:
        st.info("Awaiting first sensor readings from Node C...")

    # ── Footer ───────────────────────────────────────────────
    st.caption(
        f"Last updated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} · "
        f"Refreshes every {REFRESH_INTERVAL}s · "
        f"Adaptive Wireless Mesh Communication System v2.0"
    )


# ─────────────────────────────────────────────────────────────
#  Entry point
# ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--simulate", action="store_true",
                        help="Run in simulation mode (no hardware needed)")
    parser.add_argument("--port", type=str, default=None,
                        help="Serial port (e.g. /dev/ttyUSB0 or COM3)")
    args, _ = parser.parse_known_args()

    simulate = args.simulate or not SERIAL_AVAILABLE

    if simulate:
        t = threading.Thread(target=simulator, daemon=True)
        t.start()
        st.sidebar.info("Running in simulation mode — no hardware connected.")
    else:
        # Auto-detect port or use supplied
        port = args.port
        if port is None:
            ports = [p.device for p in serial.tools.list_ports.comports()]
            if ports:
                port = ports[0]
            else:
                st.error("No serial port found. Run with --simulate for demo mode.")
                return

        t = threading.Thread(target=serial_reader, args=(port,), daemon=True)
        t.start()

    # Streamlit auto-rerun loop
    render_dashboard()
    time.sleep(REFRESH_INTERVAL)
    st.rerun()


if __name__ == "__main__":
    main()
