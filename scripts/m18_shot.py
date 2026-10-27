# -*- coding: utf-8 -*-
"""M18 验证辅助：窗口截图 / 点击（ctypes + Pillow，不依赖 PowerShell Add-Type）。

用法：
    python m18_shot.py shot  out.png            # PrintWindow 抓整窗
    python m18_shot.py click X Y                # 窗口内坐标点击（相对窗口左上）
    python m18_shot.py drag  X1 Y1 X2 Y2        # 拖拽
    python m18_shot.py key   VK                 # 敲一个虚拟键码
    python m18_shot.py type  "文本"             # 逐字符输入（仅 ASCII 可靠）
    python m18_shot.py rect                     # 打印窗口矩形
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

from PIL import Image

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

# 进程按「每显示器 DPI 感知」运行：GetWindowRect / SetCursorPos / PrintWindow
# 全部统一到物理像素，避免 150% 缩放下只截到窗口左上角一块。
try:
    ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
except Exception:
    try:
        user32.SetProcessDPIAware()
    except Exception:
        pass

PW_RENDERFULLCONTENT = 0x00000002


def find_window(title_part="MeowAi"):
    result = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def _cb(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return True
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        if title_part in buf.value:
            result.append((hwnd, buf.value))
        return True

    user32.EnumWindows(_cb, 0)
    return result


def window_rect(hwnd):
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top


def shot(hwnd, out_path):
    # 窗口被最小化时 PrintWindow 只会回放最后一帧（看起来像「改了没生效」）：
    # 截图前先确认未最小化，必要时恢复。
    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, 9)
        time.sleep(0.5)
    left, top, width, height = window_rect(hwnd)
    hwnd_dc = user32.GetWindowDC(hwnd)
    mfc_dc = gdi32.CreateCompatibleDC(hwnd_dc)
    bitmap = gdi32.CreateCompatibleBitmap(hwnd_dc, width, height)
    gdi32.SelectObject(mfc_dc, bitmap)
    ok = user32.PrintWindow(hwnd, mfc_dc, PW_RENDERFULLCONTENT)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
            ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
            ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
            ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
        ]

    header = BITMAPINFOHEADER()
    header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    header.biWidth = width
    header.biHeight = -height  # 自上而下
    header.biPlanes = 1
    header.biBitCount = 32
    header.biCompression = 0

    buf = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(mfc_dc, bitmap, 0, height, buf, ctypes.byref(header), 0)
    image = Image.frombuffer("RGBA", (width, height), buf, "raw", "BGRA", 0, 1).convert("RGB")
    image.save(out_path)
    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mfc_dc)
    user32.ReleaseDC(hwnd, hwnd_dc)
    print("saved %s ok=%s rect=%d,%d %dx%d" % (out_path, ok, left, top, width, height))


def focus(hwnd):
    """把窗口真正提到前台：Windows 拒绝跨进程抢焦点，先用一次 Alt 敲击取得输入事件资格，
    再用 TOPMOST 抬升 z 序，最后确认 GetForegroundWindow 命中。"""
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.keybd_event(0x12, 0, 0, 0)   # ALT down
    user32.keybd_event(0x12, 0, 2, 0)   # ALT up
    user32.BringWindowToTop(hwnd)
    user32.SetForegroundWindow(hwnd)
    HWND_TOPMOST, HWND_NOTOPMOST = -1, -2
    SWP_NOMOVE, SWP_NOSIZE = 0x0002, 0x0001
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
    user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)
    time.sleep(0.2)
    return user32.GetForegroundWindow() == hwnd


def click(hwnd, x, y):
    ok = focus(hwnd)
    # focus() 里的 SW_RESTORE 可能改变窗口位置：必须**之后**再取一次矩形
    left, top, _w, _h = window_rect(hwnd)
    user32.SetCursorPos(left + x, top + y)
    time.sleep(0.12)
    user32.mouse_event(0x0002, 0, 0, 0, 0)  # LEFTDOWN
    time.sleep(0.05)
    user32.mouse_event(0x0004, 0, 0, 0, 0)  # LEFTUP
    print("click %d,%d (screen %d,%d) focused=%s" % (x, y, left + x, top + y, ok))


def drag(hwnd, x1, y1, x2, y2):
    left, top, _w, _h = window_rect(hwnd)
    ok = focus(hwnd)
    user32.SetCursorPos(left + x1, top + y1)
    time.sleep(0.1)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    steps = 20
    for i in range(1, steps + 1):
        user32.SetCursorPos(left + x1 + (x2 - x1) * i // steps,
                            top + y1 + (y2 - y1) * i // steps)
        time.sleep(0.012)
    time.sleep(0.1)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    print("drag done focused=%s" % ok)


def key(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.05)
    user32.keybd_event(vk, 0, 2, 0)


def type_text(text):
    # 只支持 ASCII：用 VkKeyScanW 逐字符
    for ch in text:
        code = user32.VkKeyScanW(ord(ch))
        vk = code & 0xFF
        shift = (code >> 8) & 0xFF
        if shift & 1:
            user32.keybd_event(0x10, 0, 0, 0)
        user32.keybd_event(vk, 0, 0, 0)
        user32.keybd_event(vk, 0, 2, 0)
        if shift & 1:
            user32.keybd_event(0x10, 0, 2, 0)
        time.sleep(0.03)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    windows = find_window("MeowAi")
    if not windows:
        print("window not found")
        return 2
    hwnd, title = windows[0]
    cmd = sys.argv[1]
    if cmd == "shot":
        shot(hwnd, sys.argv[2])
    elif cmd == "click":
        click(hwnd, int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == "drag":
        drag(hwnd, *[int(v) for v in sys.argv[2:6]])
    elif cmd == "key":
        key(int(sys.argv[2]))
    elif cmd == "type":
        type_text(sys.argv[2])
    elif cmd == "rect":
        print(title, window_rect(hwnd))
    else:
        print("unknown command")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
