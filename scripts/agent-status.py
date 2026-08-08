#!/usr/bin/env python3
"""
Collect real system data and push an agent-status dashboard to the LilyGo e-paper.

Usage:
    ./scripts/agent-status.py                          # one-shot push
    LILYGO_IP=10.0.0.189 ./scripts/agent-status.py     # custom IP
    ./scripts/agent-status.py --repo /path/to/repo     # custom repo
    ./scripts/agent-status.py --watch                  # live mode (polls every 30s)
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error
import datetime

DEFAULT_IP = os.environ.get("LILYGO_IP", "10.0.0.189")
DASHBOARD_URL = f"http://{DEFAULT_IP}/dashboard"

# Agents to monitor: name, process pattern, friendly label
AGENTS = [
    ("pi",         "pi",       "pi"),
    ("codex",      "Codex",    "Codex"),
    ("cursor",     "Cursor",   "Cursor"),
    ("openchamber","OpenChamber", "OpenChamber"),
    ("agent-browser", "Chrome for Testing", "agent-browser"),
]


def run(cmd, **kwargs):
    """Run a command, return stdout or ''."""
    try:
        return subprocess.check_output(
            cmd, shell=True, stderr=subprocess.DEVNULL,
            text=True, timeout=5, **kwargs
        ).strip()
    except Exception:
        return ""


def probe_agent(name, pattern):
    """Check if an agent is running; return status dict."""
    pids = run(f'pgrep -f "{pattern}" | head -3')
    if not pids:
        return {"running": False}

    pid_list = [p for p in pids.split() if p.isdigit()]
    if not pid_list:
        return {"running": False}

    # Get details of the primary PID
    pid = pid_list[0]
    info = run(f'ps -p {pid} -o %cpu=,%mem=,etime=,rss= 2>/dev/null')
    if not info:
        return {"running": True}

    parts = info.split()
    if len(parts) >= 4:
        return {
            "running": True,
            "cpu": f"{parts[0]}%",
            "mem": f"{parts[1]}%",
            "elapsed": parts[2],
            "rss_mb": str(max(1, int(parts[3]) // 1024)),
            "pid": pid,
        }
    return {"running": True}


def get_cwd(pid):
    """Get the working directory of a process."""
    return run(f'lsof -p {pid} 2>/dev/null | grep cwd | awk \'{{print $9}}\'')


def git_status(repo_path):
    """Get git repo status."""
    cwd = repo_path or run("pwd")
    if not cwd:
        return None

    branch = run(f'cd "{cwd}" && git branch --show-current 2>/dev/null')
    changed = run(f'cd "{cwd}" && git status --short 2>/dev/null')
    commits = run(f'cd "{cwd}" && git log --oneline -3 2>/dev/null')

    n_changed = len([l for l in changed.split('\n') if l.strip()]) if changed else 0

    return {
        "branch": branch or "unknown",
        "changed": n_changed,
        "last_commits": [l.strip() for l in commits.split('\n') if l.strip()][:3],
    }


def system_info():
    """Get system stats."""
    uptime_str = run("uptime | sed 's/.*up //' | sed 's/,.*//'")
    load = run("sysctl -n vm.loadavg 2>/dev/null | tr -d '{}'")
    disk = run("df -h / | tail -1 | awk '{print $3\"/\"$2\" (\"$5\")'}'")
    mem_total = run("sysctl -n hw.memsize 2>/dev/null")
    if mem_total:
        mem_gb = round(int(mem_total) / 1024**3)
    else:
        mem_gb = "?"
    pages_free = run("vm_stat 2>/dev/null | grep 'Pages free' | awk '{print $NF}'")
    return {
        "uptime": uptime_str,
        "load": load,
        "disk": disk,
        "mem_gb": mem_gb,
    }


def build_dashboard(repo_path=None):
    """Build the full dashboard JSON."""
    now = datetime.datetime.now().strftime("%a %b %d, %Y %H:%M")

    sections = []

    # --- Agents ---
    agent_rows = []
    pi_info = None
    pi_cwd = None
    for _, pattern, label in AGENTS:
        info = probe_agent(_, pattern)
        if not info["running"]:
            agent_rows.append([label, "offline", ""])
        else:
            status = f"running  {info.get('cpu','?')}"
            elapsed = info.get("elapsed", "")
            agent_rows.append([label, status, elapsed])

            if _ == "pi":
                pi_info = info
                pi_cwd = get_cwd(info.get("pid", ""))

    if agent_rows:
        sections.append({"header": "AGENTS", "rows": agent_rows})

    # --- Pi Session (if running) ---
    if pi_info and pi_cwd:
        sections.append({
            "header": "PI SESSION",
            "rows": [
                ["cwd", pi_cwd],
                ["pid", pi_info["pid"]],
                ["rss", f"{pi_info['rss_mb']} MB"],
            ]
        })

    # --- Repo ---
    repo = git_status(repo_path)
    if repo:
        changed_str = f"{repo['changed']} file(s)" if repo['changed'] else "clean"
        sections.append({
            "header": "REPO",
            "rows": [
                ["branch", repo["branch"]],
                ["status", changed_str],
            ]
        })
        if repo["last_commits"]:
            sections.append({
                "header": "RECENT COMMITS",
                "rows": repo["last_commits"]
            })

    # --- System ---
    sys_info = system_info()
    load_parts = sys_info["load"].split() if sys_info["load"] else []
    sections.append({
        "header": "SYSTEM",
        "rows": [
            ["uptime", sys_info["uptime"]],
            ["load", " ".join(load_parts[:3])],
            ["disk", sys_info["disk"]],
        ]
    })

    return {"title": "Agent Status", "subtitle": now, "sections": sections}


def push(data):
    """Push dashboard JSON to the display."""
    payload = json.dumps(data).encode()
    req = urllib.request.Request(
        DASHBOARD_URL,
        data=payload,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status == 200:
                print(f"✓ Dashboard sent to {DASHBOARD_URL}", file=sys.stderr)
                return True
            else:
                print(f"✗ Server returned {resp.status}", file=sys.stderr)
                return False
    except urllib.error.URLError as e:
        print(f"✗ Could not reach {DEFAULT_IP}: {e.reason}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(description="Push agent status dashboard")
    parser.add_argument("--repo", default=None, help="Path to git repo to monitor")
    parser.add_argument("--watch", action="store_true", help="Continuous mode (30s interval)")
    args = parser.parse_args()

    interval = 30
    while True:
        data = build_dashboard(args.repo)
        push(data)
        if not args.watch:
            break
        time.sleep(interval)


if __name__ == "__main__":
    main()
