"""Native Windows smoke test; uses an isolated executable/profile under build/phase3-gui.

Run after an x64 Debug build: python tools/note-view-smoke.py
Fixtures and screenshots remain in build/phase3-gui for inspection.
"""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import zlib

root = Path(__file__).resolve().parents[1]
out = root / 'build' / 'phase3-gui'
out.mkdir(parents=True, exist_ok=True)
for source in (root / 'x64' / 'Debug').iterdir():
    if source.name == 'sakura.exe' or source.suffix.lower() == '.dll':
        shutil.copy2(source, out / source.name)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

photo = out / 'photo.png'
photo.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 320, 120, 8, 2, 0, 0, 0))
                 + chunk(b'IDAT', zlib.compress((b'\0' + bytes((200, 40, 20)) * 320) * 120)) + chunk(b'IEND', b''))
document = out / 'note.txt'
original = ('before ![inline](' + photo.as_posix() + ')\r\n![photo](' + photo.as_posix()
            + ')\r\nafter\r\n' + 'tail\r\n' * 100 + 'wide' * 500).encode()
document.write_bytes(original)

user = c.WinDLL('user32', use_last_error=True)
gdi = c.WinDLL('gdi32', use_last_error=True)
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumWindows.argtypes = [callback, w.LPARAM]
user.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user.GetDC.argtypes = [w.HWND]
user.GetDC.restype = w.HDC
user.ReleaseDC.argtypes = [w.HWND, w.HDC]
user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.SendMessageW.restype = c.c_ssize_t
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.SetForegroundWindow.argtypes = [w.HWND]
user.MoveWindow.argtypes = [w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.BOOL]
user.IsWindowVisible.argtypes = [w.HWND]
user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
user.SetCursorPos.argtypes = [c.c_int, c.c_int]
class GuiInfo(c.Structure):
    _fields_ = [('cbSize', w.DWORD), ('flags', w.DWORD), ('active', w.HWND), ('focus', w.HWND),
                ('capture', w.HWND), ('menuOwner', w.HWND), ('moveSize', w.HWND), ('caret', w.HWND), ('rect', w.RECT)]
user.GetGUIThreadInfo.argtypes = [w.DWORD, c.POINTER(GuiInfo)]
gdi.CreateCompatibleDC.argtypes = [w.HDC]
gdi.CreateCompatibleDC.restype = w.HDC
gdi.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]
gdi.CreateCompatibleBitmap.restype = w.HBITMAP
gdi.SelectObject.argtypes = [w.HDC, w.HGDIOBJ]
gdi.SelectObject.restype = w.HGDIOBJ
gdi.BitBlt.argtypes = [w.HDC, c.c_int, c.c_int, c.c_int, c.c_int, w.HDC, c.c_int, c.c_int, w.DWORD]
gdi.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT]
gdi.DeleteObject.argtypes = [w.HGDIOBJ]
gdi.DeleteDC.argtypes = [w.HDC]

def windows(parent=None):
    result = []
    @callback
    def collect(hwnd, _):
        name = c.create_unicode_buffer(256)
        user.GetClassNameW(hwnd, name, len(name))
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        result.append((hwnd, name.value, pid.value))
        return True
    if parent is None:
        user.EnumWindows(collect, 0)
    else:
        user.EnumChildWindows(parent, collect, 0)
    return result

def capture(view, label):
    rect = w.RECT()
    user.GetClientRect(view, c.byref(rect))
    width, height = rect.right, rect.bottom
    dc = user.GetDC(view)
    memory = gdi.CreateCompatibleDC(dc)
    bitmap = gdi.CreateCompatibleBitmap(dc, width, height)
    old = gdi.SelectObject(memory, bitmap)
    gdi.BitBlt(memory, 0, 0, width, height, dc, 0, 0, 0x00CC0020)
    gdi.SelectObject(memory, old)
    header = struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
    info = c.create_string_buffer(header)
    pixels = c.create_string_buffer(width * height * 4)
    assert gdi.GetDIBits(memory, bitmap, 0, height, pixels, info, 0)
    gdi.DeleteObject(bitmap)
    gdi.DeleteDC(memory)
    user.ReleaseDC(view, dc)
    raw = pixels.raw
    (out / (label + '.bmp')).write_bytes(struct.pack('<2sIHHI', b'BM', 54 + len(raw), 0, 0, 54) + header + raw)
    rgb = bytearray(width * height * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
    scanlines = b''.join(b'\0' + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    (out / (label + '.png')).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                                       + chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))
    points = [(i // 4 % width, i // 4 // width) for i in range(0, len(raw), 4) if raw[i:i+3] == bytes((20, 40, 200))]
    if not points:
        return None
    return min(x for x, _ in points), min(y for _, y in points), max(x for x, _ in points), max(y for _, y in points)

def wait_for(predicate, description):
    for _ in range(100):
        result = predicate()
        if result:
            return result
        time.sleep(0.1)
    raise AssertionError(f'{description}; process={process.poll()}')

profile = f'phase3-smoke-{os.getpid()}'
process = subprocess.Popen([str(out / 'sakura.exe'), '-PROF=' + profile, str(document)], cwd=out)
frame = None
try:
    frame = wait_for(lambda: next((hwnd for hwnd, name, pid in windows()
                                 if pid == process.pid and user.IsWindowVisible(hwnd) and name.startswith('TextEditorWindow')), None), 'editor window')
    user.MoveWindow(frame, 20, 20, 1100, 760, True)
    user.SetForegroundWindow(frame)
    view = wait_for(lambda: next((hwnd for hwnd, name, _ in windows(frame)
                                if name.startswith('SakuraView') and user.IsWindowVisible(hwnd)), None), 'text view')
    def command(code):
        user.SendMessageW(frame, 0x111, code, 0)
        time.sleep(0.1)
    def caret_point():
        info = GuiInfo()
        info.cbSize = c.sizeof(info)
        assert user.GetGUIThreadInfo(user.GetWindowThreadProcessId(view, None), c.byref(info))
        return info.rect.left, info.rect.top
    def hover(x, y):
        point = w.POINT(x, y)
        user.ClientToScreen(view, c.byref(point))
        user.SetCursorPos(point.x + 200, point.y + 200)
        time.sleep(0.1)
        user.SetCursorPos(point.x, point.y)
    def popup():
        return any(name == 'SakuraImagePreview' and pid == process.pid and user.IsWindowVisible(hwnd)
                   for hwnd, name, pid in windows())
    command(31141)  # Unwrapped text also gives horizontal scrolling a measurable range.
    command(30350)
    assert capture(view, 'off') is None
    command(30312)
    link_caret = caret_point()
    hover(link_caret[0] + 12, link_caret[1] + 4)
    wait_for(popup, 'OFF standalone image hover')
    command(31105)
    assert not popup(), 'ON did not cancel the popup'
    command(30350)
    image = wait_for(lambda: capture(view, 'on'), 'inline image after enabling Note View')
    assert image[2] - image[0] + 1 == 320 and image[3] - image[1] + 1 == 120, image
    inline_caret = caret_point()
    hover(inline_caret[0] + 85, inline_caret[1] + 4)
    time.sleep(0.8)
    assert not popup(), 'ON allowed an inline image hover'
    hover(link_caret[0] + 12, link_caret[1] + 4)
    time.sleep(0.8)
    assert not popup(), 'ON allowed a standalone image hover'
    user.SendMessageW(view, 0x114, 1, 0)
    time.sleep(0.1)
    assert capture(view, 'horizontal') == image, 'image moved horizontally'
    user.SendMessageW(view, 0x114, 6, 0)
    command(30401)
    assert capture(view, 'selection') == image, 'image was painted with selection color'
    click = (image[1] + 20) * 65536 + image[0] + 20
    user.SendMessageW(view, 0x201, 1, click)
    user.SendMessageW(view, 0x202, 0, click)
    assert caret_point() == link_caret, (caret_point(), link_caret)
    command(30350)
    # Scroll until the viewport cuts through the middle of the image.
    partial = None
    for _ in range(20):
        user.SendMessageW(view, 0x115, 1, 0)
        time.sleep(0.05)
        partial = capture(view, 'partial')
        if partial and partial[3] - partial[1] + 1 < 120:
            break
    assert partial and partial[0] == image[0] and partial[3] - partial[1] + 1 < 120, partial
    user.SendMessageW(view, 0x115, 6, 0)
    time.sleep(0.1)
    assert capture(view, 'scroll-restored') == image
    # Break the standalone link through normal text input, then restore it through Undo.
    command(30350)
    command(30312)
    user.SendMessageW(view, 0x102, ord('x'), 0)
    time.sleep(0.3)
    assert capture(view, 'broken') is None
    command(30210)
    restored = wait_for(lambda: capture(view, 'undo'), 'image restored by Undo')
    assert restored == image, (image, restored)
    command(30103)
    assert document.read_bytes() == original, 'virtual rows leaked into saved text'
    # Both split panes display images and maintain independent vertical positions.
    command(31311)
    panes = [(hwnd, name) for hwnd, name, _ in windows(frame) if name.startswith('SakuraView') and user.IsWindowVisible(hwnd)]
    assert len(panes) == 2, panes
    for hwnd, _ in panes:
        user.SendMessageW(hwnd, 0x115, 6, 0)
        wait_for(lambda hwnd=hwnd: capture(hwnd, 'split-' + str(hwnd)), 'image in split pane')
    right_before = capture(panes[1][0], 'split-right-before')
    user.SendMessageW(panes[0][0], 0x115, 1, 0)
    time.sleep(0.1)
    assert capture(panes[1][0], 'split-right-after') == right_before
    command(31311)
    view = next(hwnd for hwnd, name, _ in windows(frame) if name.startswith('SakuraView') and user.IsWindowVisible(hwnd))
    command(30350)
    command(31104)
    mini_views = [hwnd for hwnd, name, _ in windows(frame) if name.startswith('SakuraView') and user.IsWindowVisible(hwnd) and hwnd != view]
    assert mini_views and all(capture(hwnd, 'minimap') is None for hwnd in mini_views)
    command(31104)
    command(30151)
    assert capture(frame, 'print-preview') is None, 'image appeared in print preview'
    command(30151)
    wait_for(lambda: capture(view, 'after-print'), 'image restored after print preview')
    # External image edits remain cached until a document reload.
    photo.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 160, 80, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress((b'\0' + bytes((200, 40, 20)) * 160) * 80)) + chunk(b'IEND', b''))
    command(31360)
    cached = capture(view, 'cached')
    assert cached and cached[2] - cached[0] + 1 == 320
    command(30119)
    def reloaded():
        box = capture(view, 'reloaded')
        return box if box and box[2] - box[0] + 1 == 160 and box[3] - box[1] + 1 == 80 else None
    wait_for(reloaded, 'document reload refreshes images while keeping Note View enabled')
    assert document.read_bytes() == original
    command(31105)
    assert capture(view, 'off-again') is None
    command(30350)
    inline_caret = caret_point()
    hover(inline_caret[0] + 85, inline_caret[1] + 4)
    wait_for(popup, 'OFF inline image hover restored')
    print('PASS: native images, ON/OFF hover, image click, selection exclusion, horizontal/partial scrolling, edits/Undo/save, split panes, minimap, print preview and reload')
finally:
    if frame:
        user.PostMessageW(frame, 0x10, 0, 0)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.terminate()
    for hwnd, name, _ in windows():
        if name.startswith('CControlTray') and name.endswith(profile):
            user.PostMessageW(hwnd, 0x10, 0, 0)
