#!/usr/bin/env python3
"""Draw the demo graph from the CSV written by vigiad.

Usage: plot.py data.csv events.csv output.png

One panel per measure (they have different units, so they do not share a
y-axis). Every panel shows the measured value, the baseline band
(mu +/- 2 sigma) and the periods where a known load was injected.
"""
import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SERIES = "#2a78d6"     # measured value
BAND = "#8a8985"       # baseline band
LOAD = "#eda100"       # injected load periods
ALERT = "#e34948"      # alert raised by vigiad
INK = "#0b0b0b"
MUTED = "#52514e"
GRID = "#e4e3df"
SURFACE = "#fcfcfb"

PANELS = [
    ("cpu_pct", "Uso de CPU (%)"),
    ("mem_pct", "Uso de RAM (%)"),
    ("temp_c", "Temperatura CPU (°C)"),
]

CAUSES = {
    "cpu_pct": "uso de CPU", "mem_pct": "uso de RAM", "swap_pct": "swap",
    "temp_c": "temperatura", "psi_cpu_pct": "presión de CPU",
    "psi_mem_pct": "presión de memoria", "psi_io_pct": "presión de E/S",
    "latency_us": "latencia",
}


def number(text):
    return float(text) if text else float("nan")


def load(csv_path, events_path):
    with open(csv_path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit(f"{csv_path}: no samples")
    t0 = float(rows[0]["time"])
    for r in rows:
        r["t"] = float(r["time"]) - t0

    events = []
    with open(events_path, newline="") as f:
        for e in csv.DictReader(f):
            events.append((float(e["start"]) - t0, float(e["end"]) - t0,
                           e["label"]))
    return rows, events


def alert_spans(rows):
    """Periods where vigiad kept the alert raised: (start, end, cause)."""
    spans, start, causes = [], None, []
    for r in rows:
        on = r["alert"] == "1"
        if on and start is None:
            start, causes = r["t"], []
        if on:
            causes.append(r["cause"])
        if not on and start is not None:
            spans.append((start, r["t"], max(set(causes), key=causes.count)))
            start = None
    if start is not None:
        spans.append((start, rows[-1]["t"], max(set(causes), key=causes.count)))
    return spans


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    rows, events = load(sys.argv[1], sys.argv[2])
    monitor = [r for r in rows if r["phase"] == "monitor"]
    if not monitor:
        sys.exit("no monitoring samples: calibration never finished")
    t_ready = monitor[0]["t"]
    t_end = rows[-1]["t"]
    spans = alert_spans(rows)

    plt.rcParams.update({
        "font.size": 10, "axes.edgecolor": GRID, "axes.labelcolor": MUTED,
        "xtick.color": MUTED, "ytick.color": MUTED, "text.color": INK,
        "axes.facecolor": SURFACE, "figure.facecolor": SURFACE,
        "savefig.facecolor": SURFACE,
    })
    fig, axes = plt.subplots(len(PANELS) + 1, 1, figsize=(10, 9.5), sharex=True,
                             gridspec_kw={"hspace": 0.32})

    t = [r["t"] for r in rows]
    for ax, (key, title) in zip(axes, PANELS):
        mu = number(monitor[0]["mu_" + key])
        sigma = number(monitor[0]["sigma_" + key])
        ax.fill_between([t_ready, t_end], mu - 2 * sigma, mu + 2 * sigma,
                        color=BAND, alpha=0.25, linewidth=0,
                        label="Baseline (μ ± 2σ)")
        ax.plot(t, [number(r[key]) for r in rows], color=SERIES, linewidth=2,
                label="Medido")
        ax.set_title(title, loc="left", fontsize=10, color=INK)
        ax.margins(y=0.15)

    ax = axes[-1]
    ax.plot([r["t"] for r in monitor], [number(r["index"]) for r in monitor],
            color=SERIES, linewidth=2, label="Índice de degradación")
    ax.axhline(2.0, color=MUTED, linewidth=1, linestyle="--")
    ax.annotate("umbral de alerta (2σ)", xy=(t_end, 2.0), xytext=(0, 4),
                textcoords="offset points", ha="right", fontsize=8,
                color=MUTED)
    ax.set_title("Índice de degradación (desviaciones estándar sobre la "
                 "baseline)", loc="left", fontsize=10, color=INK)
    ax.set_xlabel("Tiempo (s)")
    peak = max(number(r["index"]) for r in monitor)
    ymax = max(peak, 2.0) * 1.45
    ax.set_ylim(top=ymax)
    for start, end, cause in spans:
        ax.plot([start, end], [ymax * 0.93] * 2, color=ALERT, linewidth=5,
                solid_capstyle="butt")
        ax.annotate(f"ALERTA · causa: {CAUSES.get(cause, cause)}",
                    xy=(start, ymax * 0.93), xytext=(0, -13),
                    textcoords="offset points", fontsize=8, color=INK)

    for i, ax in enumerate(axes):
        ax.axvspan(0, t_ready, color=BAND, alpha=0.10, linewidth=0)
        for start, end, label in events:
            ax.axvspan(start, end, color=LOAD, alpha=0.18, linewidth=0)
            if i == 0:
                ax.annotate(label.replace("CPU load", "Carga de CPU")
                                 .replace("Memory load", "Carga de memoria"),
                            xy=((start + end) / 2, 1.0),
                            xycoords=("data", "axes fraction"),
                            xytext=(0, -12), textcoords="offset points",
                            ha="center", fontsize=9, color=INK)
        if i == 0:
            ax.annotate("Calibración", xy=(t_ready / 2, 1.0),
                        xycoords=("data", "axes fraction"), xytext=(0, -12),
                        textcoords="offset points", ha="center", fontsize=9,
                        color=MUTED)
        ax.grid(axis="y", color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)
        ax.spines[["top", "right"]].set_visible(False)
        ax.set_xlim(0, t_end)

    axes[0].legend(loc="upper right", frameon=False, fontsize=8, ncols=2,
                   bbox_to_anchor=(1.0, 1.22))
    fig.suptitle("Vigía detecta cada carga inyectada y vuelve a normal al "
                 "retirarla", x=0.125, ha="left", fontsize=13, weight="bold")
    fig.savefig(sys.argv[3], dpi=150, bbox_inches="tight")

    # Text summary: how long each injected load took to be detected.
    print(f"graph written to {sys.argv[3]}")
    for start, end, label in events:
        hit = next((s for s in spans if s[0] < end and s[1] > start), None)
        if hit:
            print(f"  {label}: injected at {start:.0f}s, alert at {hit[0]:.0f}s "
                  f"(+{hit[0] - start:.0f}s), cleared at {hit[1]:.0f}s "
                  f"(+{hit[1] - end:.0f}s after the load ended), "
                  f"cause: {hit[2]}")
        else:
            print(f"  {label}: injected at {start:.0f}s, NOT detected")
    outside = [s for s in spans
               if not any(s[0] < e[1] + 30 and s[1] > e[0] for e in events)]
    print(f"  alerts with no injected load nearby: {len(outside)}")


if __name__ == "__main__":
    main()
