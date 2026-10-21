#!/usr/bin/env python3
"""Capture TrafficMonitor's taskbar window for visual verification."""

from __future__ import annotations

import csv
import ctypes
import subprocess
import sys
from pathlib import Path

import win32gui
import win32process
import win32ui
from PIL import Image


WINDOW_TITLE = "TrafficMonitorTaskbarWindow"


def make_dpi_aware() -> None:
    # Without this, GetWindowRect returns virtualized (scaled-down) coordinates
    # on high-DPI displays and the capture bitmap clips the window content.
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)  # PER_MONITOR_DPI_AWARE
    except OSError:
        ctypes.windll.user32.SetProcessDPIAware()


def trafficmonitor_pids() -> set[int]:
    output = subprocess.check_output(
        ["tasklist", "/FI", "IMAGENAME eq TrafficMonitor.exe", "/FO", "CSV", "/NH"],
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    pids: set[int] = set()
    for row in csv.reader(output.splitlines()):
        if len(row) >= 2 and row[0].lower() == "trafficmonitor.exe":
            pids.add(int(row[1]))
    return pids


def find_window() -> int:
    pids = trafficmonitor_pids()
    candidates: list[int] = []

    def collect(hwnd: int, _extra: object) -> bool:
        try:
            _, pid = win32process.GetWindowThreadProcessId(hwnd)
        except Exception:
            return True
        if pid in pids:
            candidates.append(hwnd)
        return True

    win32gui.EnumWindows(collect, None)
    for class_name in ("Shell_TrayWnd", "Shell_SecondaryTrayWnd"):
        tray_root = win32gui.FindWindow(class_name, None)
        if tray_root:
            win32gui.EnumChildWindows(tray_root, collect, None)
    for hwnd in list(candidates):
        win32gui.EnumChildWindows(hwnd, collect, None)

    for hwnd in candidates:
        if win32gui.IsWindowVisible(hwnd) and win32gui.GetWindowText(hwnd) == WINDOW_TITLE:
            return hwnd
    raise RuntimeError(f"{WINDOW_TITLE} was not found")


def capture_window(hwnd: int) -> Image.Image:
    left, top, right, bottom = win32gui.GetWindowRect(hwnd)
    width = right - left
    height = bottom - top

    window_dc_handle = win32gui.GetWindowDC(hwnd)
    window_dc = win32ui.CreateDCFromHandle(window_dc_handle)
    memory_dc = window_dc.CreateCompatibleDC()
    bitmap = win32ui.CreateBitmap()
    bitmap.CreateCompatibleBitmap(window_dc, width, height)
    memory_dc.SelectObject(bitmap)

    # PW_RENDERFULLCONTENT captures the window itself without screen occlusion.
    printed = ctypes.windll.user32.PrintWindow(hwnd, memory_dc.GetSafeHdc(), 2)
    if printed == 0:
        ctypes.windll.user32.PrintWindow(hwnd, memory_dc.GetSafeHdc(), 0)

    bitmap_bits = bitmap.GetBitmapBits(True)
    image = Image.frombuffer("RGB", (width, height), bitmap_bits, "raw", "BGRX", 0, 1)

    memory_dc.DeleteDC()
    window_dc.DeleteDC()
    win32gui.ReleaseDC(hwnd, window_dc_handle)
    win32gui.DeleteObject(bitmap.GetHandle())
    return image



def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} OUTPUT.png", file=sys.stderr)
        return 2

    hwnd = find_window()
    make_dpi_aware()
    rect = win32gui.GetWindowRect(hwnd)
    image = capture_window(hwnd)
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output)
    print(f"Captured hwnd={hwnd} rect={rect} to {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
