#!/usr/bin/env python3
"""Verify whether TrafficMonitor's taskbar window is really embedded in the Windows taskbar."""

from __future__ import annotations

import csv
import ctypes
import subprocess
import sys
from dataclasses import dataclass

import win32con
import win32gui
import win32process


PROCESS_NAME = "TrafficMonitor.exe"
TRAY_CLASSES = {"Shell_TrayWnd", "Shell_SecondaryTrayWnd"}


@dataclass(frozen=True)
class WindowInfo:
    hwnd: int
    pid: int
    class_name: str
    text: str
    rect: tuple[int, int, int, int]
    visible: bool
    topmost: bool
    parent_chain: tuple[int, ...]


def trafficmonitor_pids() -> set[int]:
    output = subprocess.check_output(
        ["tasklist", "/FI", f"IMAGENAME eq {PROCESS_NAME}", "/FO", "CSV", "/NH"],
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    pids: set[int] = set()
    for row in csv.reader(output.splitlines()):
        if len(row) >= 2 and row[0].lower() == PROCESS_NAME.lower():
            pids.add(int(row[1]))
    return pids


def describe_window(hwnd: int, target_pids: set[int]) -> WindowInfo | None:
    try:
        _, pid = win32process.GetWindowThreadProcessId(hwnd)
    except Exception:
        return None
    if pid not in target_pids:
        return None

    chain: list[int] = []
    current = hwnd
    seen = {current}
    while True:
        # GetParent can return the owner instead of the real parent for this
        # MFC dialog. GA_PARENT reports the actual taskbar ancestor.
        parent = win32gui.GetAncestor(current, 1)
        if not parent or parent in seen:
            break
        chain.append(parent)
        seen.add(parent)
        current = parent

    ex_style = win32gui.GetWindowLong(hwnd, win32con.GWL_EXSTYLE)
    return WindowInfo(
        hwnd=hwnd,
        pid=pid,
        class_name=win32gui.GetClassName(hwnd),
        text=win32gui.GetWindowText(hwnd),
        rect=win32gui.GetWindowRect(hwnd),
        visible=bool(win32gui.IsWindowVisible(hwnd)),
        topmost=bool(ex_style & win32con.WS_EX_TOPMOST),
        parent_chain=tuple(chain),
    )


def enum_windows(target_pids: set[int]) -> dict[int, WindowInfo]:
    found: dict[int, WindowInfo] = {}

    def callback(hwnd: int, _extra: object) -> bool:
        info = describe_window(hwnd, target_pids)
        if info is not None:
            found[hwnd] = info
        return True

    win32gui.EnumWindows(callback, None)

    tray_roots = tray_window_roots()
    for tray_root in tray_roots.values():
        win32gui.EnumChildWindows(tray_root, callback, None)

    return found


def tray_window_roots() -> dict[str, int]:
    roots: dict[str, int] = {}
    for class_name in TRAY_CLASSES:
        hwnd = win32gui.FindWindow(class_name, None)
        if hwnd:
            roots[class_name] = hwnd
    return roots


def intersects(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> bool:
    return a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1]


def main() -> int:
    pids = trafficmonitor_pids()
    if not pids:
        print(f"FAIL: {PROCESS_NAME} is not running.")
        return 2

    tray_roots = tray_window_roots()
    if not tray_roots:
        print("FAIL: no Windows taskbar root window was found.")
        return 2

    windows = enum_windows(pids)
    if not windows:
        print("FAIL: no visible TrafficMonitor window was found.")
        return 2

    embedded: list[WindowInfo] = []
    print(f"TrafficMonitor PID(s): {', '.join(map(str, sorted(pids)))}")
    print("Taskbar root(s):")
    for class_name, hwnd in tray_roots.items():
        print(f"  {class_name}: hwnd={hwnd}, rect={win32gui.GetWindowRect(hwnd)}")

    print("TrafficMonitor window(s):")
    for info in sorted(windows.values(), key=lambda item: item.hwnd):
        parent_desc = " -> ".join(map(str, info.parent_chain)) or "(none)"
        print(
            f"  hwnd={info.hwnd} pid={info.pid} class={info.class_name!r} text={info.text!r} "
            f"rect={info.rect} visible={info.visible} topmost={info.topmost} parents={parent_desc}"
        )
        parent_set = set(info.parent_chain)
        for tray_root in tray_roots.values():
            if tray_root in parent_set and info.visible and intersects(
                info.rect, win32gui.GetWindowRect(tray_root)
            ):
                embedded.append(info)
                break

    if embedded:
        print("PASS: at least one TrafficMonitor window is a visible descendant of the taskbar.")
        return 0

    print(
        "FAIL: TrafficMonitor is visible only as a top-level/topmost overlay or outside the taskbar hierarchy."
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
