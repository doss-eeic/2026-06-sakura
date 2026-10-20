"""Native Windows smoke test; uses an isolated executable/profile under build/phase3-gui.

Run after an x64 Debug build: python tools/note-view-smoke.py
Fixtures and screenshots remain in build/phase3-gui for inspection.
"""
import ctypes as c
from ctypes import wintypes as w
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import zlib

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bin-dir', type=Path, default=root / 'x64' / 'Debug')
parser.add_argument('--output-dir', type=Path, default=root / 'build' / 'phase3-gui')
parser.add_argument('--photo-regression', action='store_true', help='Test an image in a different directory, its context menu and height dialog')
parser.add_argument('--review-regression', action='store_true', help='Check cached Undo geometry, 3/18/40 row input and backward selection after a height edit')
args = parser.parse_args()
args.photo_regression = args.photo_regression or args.review_regression
session = w.DWORD()
if c.windll.kernel32.ProcessIdToSessionId(os.getpid(), c.byref(session)) and session.value == 0:
    parser.error('Run this GUI test from the Windows desktop; SSH session 0 cannot display the editor.')
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
for source in args.bin_dir.resolve().iterdir():
    if source.name == 'sakura.exe' or source.suffix.lower() == '.dll':
        shutil.copy2(source, out / source.name)

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

if args.photo_regression:
    (out / 'external-pictures').mkdir(exist_ok=True)
photo = out / 'external-pictures' / 'photo.png' if args.photo_regression else out / 'photo.png'
photo_height = 20 if args.review_regression else 120
photo.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 320, photo_height, 8, 2, 0, 0, 0))
                 + chunk(b'IDAT', zlib.compress((b'\0' + bytes((200, 40, 20)) * 320) * photo_height)) + chunk(b'IEND', b''))
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
user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
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
user.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
user.GetDlgCtrlID.argtypes = [w.HWND]
user.GetMenuItemCount.argtypes = [w.HMENU]
user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT]
user.GetMenuItemRect.argtypes = [w.HWND, w.HMENU, w.UINT, c.POINTER(w.RECT)]
user.mouse_event.argtypes = [w.DWORD, w.DWORD, w.DWORD, w.DWORD, c.c_ulonglong]
user.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
user.SetCursorPos.argtypes = [c.c_int, c.c_int]
user.GetForegroundWindow.restype = w.HWND
user.WindowFromPoint.argtypes = [w.POINT]
user.WindowFromPoint.restype = w.HWND
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
        # These are separate key presses, not auto-repeat (whose step is configurable).
        user.SendMessageW(view, 0x101, 0, 0)
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
    def activate():
        if user.GetForegroundWindow() == frame:
            return
        # A caption click grants foreground activation without entering Alt menu mode.
        rect = w.RECT()
        assert user.GetWindowRect(frame, c.byref(rect))
        user.SetCursorPos(rect.left + 150, rect.top + 12)
        user.mouse_event(2, 0, 0, 0, 0)
        user.mouse_event(4, 0, 0, 0, 0)
        wait_for(lambda: user.GetForegroundWindow() == frame, 'foreground editor')
    if args.photo_regression:
        activate()
        command(31141)
        command(30350)
        command(31105)
        image = wait_for(lambda: capture(view, 'external-photo'), 'photo in a different folder displayed')
        time.sleep(.5)
        image = capture(view, 'external-photo-settled')
        if args.review_regression:
            before = image
            command(30350)
            command(30312)
            user.SendMessageW(view, 0x102, ord('x'), 0)
            time.sleep(.2)
            assert capture(view, 'broken-link') is None
            command(30210)
            command(30350)
            time.sleep(.5)
            image = capture(view, 'cached-undo')
            assert image == before, ('Undo changed cached image geometry', before, image)
        click = (image[1] + 10) * 65536 + image[0] + 10
        def context_menu():
            activate()
            # Use real button events; TrackPopupMenu needs the matching release.
            pointer = w.POINT(image[0] + 10, image[1] + 10)
            user.ClientToScreen(view, c.byref(pointer))
            user.SetCursorPos(pointer.x, pointer.y)
            user.mouse_event(8, 0, 0, 0, 0)
            user.mouse_event(16, 0, 0, 0, 0)
            menu_window = wait_for(lambda: next((hwnd for hwnd, name, pid in windows()
                                                 if name == '#32768' and pid == process.pid and user.IsWindowVisible(hwnd)), None),
                                   'photo context menu')
            menu = user.SendMessageW(menu_window, 0x01E1, 0, 0)
            assert menu and user.GetMenuItemCount(menu) == 5
            labels = []
            for position in (0, 1, 3, 4):
                label = c.create_unicode_buffer(128)
                user.GetMenuStringW(menu, position, label, len(label), 0x400)
                labels.append(label.value)
            assert labels == ['ペイントで開く', '画像を更新', '高さを変更...', '画像のパスをコピー'], labels
            return menu
        def choose(menu, index):
            item = w.RECT()
            assert user.GetMenuItemRect(view, menu, index, c.byref(item))
            user.SetCursorPos((item.left + item.right) // 2, (item.top + item.bottom) // 2)
            user.mouse_event(2, 0, 0, 0, 0)
            user.mouse_event(4, 0, 0, 0, 0)
        def set_height(rows):
            choose(context_menu(), 3)
            dialog = wait_for(lambda: next((hwnd for hwnd, name, pid in windows()
                                            if name == '#32770' and pid == process.pid and user.IsWindowVisible(hwnd)), None),
                              'photo height dialog')
            edit = next(hwnd for hwnd, name, _ in windows(dialog) if name == 'Edit')
            assert user.SendMessageW(edit, 0xD5, 0, 0) >= 2, 'height input is restricted to one digit'
            user.SendMessageW(edit, 0xB1, 0, -1)
            for ch in str(rows):
                user.SendMessageW(edit, 0x102, ord(ch), 0)
            user.SendMessageW(dialog, 0x111, 1, 0)
            wait_for(lambda: not user.IsWindowVisible(dialog), 'photo height dialog closed')
            command(30103)
            assert f' <!-- sakura:rows={rows} -->'.encode() in document.read_bytes(), 'height annotation was not saved'
        if args.review_regression:
            for rows in (3, 18, 40):
                set_height(rows)
                image = capture(view, f'height-{rows}')
            command(30350)
            for _ in range(4): command(30314)
            for _ in range(4): command(30413)
            set_height(18)
            command(30414)
            user.SendMessageW(view, 0x102, ord('Z'), 0)
            command(30103)
            assert document.read_bytes().startswith(b'bZre '), 'height edit destroyed backward selection anchor'
            print('PASS: cached Undo geometry, 3 -> 18 -> 40 height input and backward selection continuity')
            raise SystemExit(0)
        set_height(18)
        user.PostMessageW(view, 0x203, 1, click)  # Open the external photo by double-clicking its block.
        def paint_window():
            for hwnd, _, _ in windows():
                title = c.create_unicode_buffer(256)
                user.GetWindowTextW(hwnd, title, len(title))
                if 'photo.png' in title.value and 'Paint' in title.value:
                    return hwnd
            return None
        paint = wait_for(paint_window, 'Paint opened photo from a different folder')
        user.PostMessageW(paint, 0x10, 0, 0)
        print('PASS: external-folder photo, right-click menu, saved height and Paint launch')
        raise SystemExit(0)
    command(31141)  # Unwrapped text also gives horizontal scrolling a measurable range.
    command(30350)
    assert capture(view, 'off') is None
    command(30312)
    link_caret = caret_point()
    activate()
    hover(link_caret[0] + 12, link_caret[1] + 4)
    wait_for(popup, 'OFF standalone image hover')
    command(31105)
    assert not popup(), 'ON did not cancel the popup'
    command(30350)
    image = wait_for(lambda: capture(view, 'on'), 'inline image after enabling Note View')
    image_width, image_height = image[2] - image[0] + 1, image[3] - image[1] + 1
    assert 320 < image_width <= 640 and abs(image_width / image_height - 320 / 120) < .04, image
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
    selected_caret = caret_point()
    click = (image[1] + 20) * 65536 + image[0] + 20
    user.SendMessageW(view, 0x201, 1, click)
    user.SendMessageW(view, 0x202, 0, click)
    assert caret_point() == selected_caret, (caret_point(), selected_caret)
    command(30350)
    # Scroll until the viewport cuts through the middle of the image.
    partial = None
    for _ in range(20):
        user.SendMessageW(view, 0x115, 1, 0)
        time.sleep(0.05)
        partial = capture(view, 'partial')
        if partial and partial[3] - partial[1] + 1 < image_height:
            break
    assert partial and partial[0] == image[0] and partial[3] - partial[1] + 1 < image_height, partial
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
    # External image edits are reflected on reload and retain the new aspect ratio.
    photo.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 160, 80, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress((b'\0' + bytes((200, 40, 20)) * 160) * 80)) + chunk(b'IEND', b''))
    command(31360)
    cached = capture(view, 'cached')
    assert cached
    command(30119)
    def reloaded():
        box = capture(view, 'reloaded')
        return box if box and abs((box[2] - box[0] + 1) / (box[3] - box[1] + 1) - 2) < .04 else None
    wait_for(reloaded, 'document reload refreshes images while keeping Note View enabled')
    assert document.read_bytes() == original
    command(31105)
    assert capture(view, 'off-again') is None
    command(30350)
    inline_caret = caret_point()
    hover(inline_caret[0] + 85, inline_caret[1] + 4)
    wait_for(popup, 'OFF inline image hover restored')
    # The new command uses the editor's own modal input dialog and creates a real PNG.
    board_name = f'phase3_board_{os.getpid()}'
    board = out / (board_name + '.png')
    user.PostMessageW(frame, 0x111, 30797, 0)
    dialog = wait_for(lambda: next((hwnd for hwnd, name, pid in windows()
                                    if name == '#32770' and pid == process.pid and user.IsWindowVisible(hwnd)), None),
                      'freeboard name dialog')
    edit = next(hwnd for hwnd, name, _ in windows(dialog) if name == 'Edit')
    def current_input():
        text = c.create_unicode_buffer(256)
        user.GetWindowTextW(edit, text, len(text))
        return text.value
    time.sleep(0.4)  # WM_INITDIALOG and its SetDlgData may still be running when the window becomes visible.
    user.SendMessageW(edit, 0xB1, 0, -1)  # EM_SETSEL: replace the default timestamp through the edit control.
    for ch in board_name:
        user.SendMessageW(edit, 0x102, ord(ch), 0)
    assert user.SendMessageW(edit, 0xE, 0, 0) == len(board_name)
    user.SendMessageW(dialog, 0x111, 1, 0)
    wait_for(lambda: board.exists() and board.stat().st_size > 40 and board.read_bytes()[-8:-4] == b'IEND',
             'white PNG completed by editor')
    content = board.read_bytes()
    assert content[:8] == b'\x89PNG\r\n\x1a\n'
    assert struct.unpack('>II', content[16:24]) == (1200, 800)
    assert content[24:29] == bytes((8, 6, 0, 0, 0)), 'expected non-interlaced RGBA PNG'
    payload = bytearray()
    cursor = 8
    while cursor < len(content):
        length = struct.unpack('>I', content[cursor:cursor + 4])[0]
        kind = content[cursor + 4:cursor + 8]
        if kind == b'IDAT':
            payload.extend(content[cursor + 8:cursor + 8 + length])
        cursor += length + 12
    scan = zlib.decompress(payload)
    stride = 1200 * 4
    assert len(scan) == (stride + 1) * 800
    previous = bytearray(stride)
    for y in range(800):
        start = y * (stride + 1)
        filter_kind = scan[start]
        row = bytearray(scan[start + 1:start + 1 + stride])
        assert filter_kind in range(5)
        for x in range(stride):
            a = row[x - 4] if x >= 4 else 0
            b = previous[x]
            corner = previous[x - 4] if x >= 4 else 0
            if filter_kind == 1:
                row[x] = (row[x] + a) & 255
            elif filter_kind == 2:
                row[x] = (row[x] + b) & 255
            elif filter_kind == 3:
                row[x] = (row[x] + (a + b) // 2) & 255
            elif filter_kind == 4:
                p = a + b - corner
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - corner)
                row[x] = (row[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else corner)) & 255
        assert all(channel == 255 for channel in row), f'non-white or transparent pixel on row {y}'
        previous = row
    wait_for(lambda: not user.IsWindowVisible(dialog), 'freeboard dialog closed')
    command(30103)
    assert ('![' + board_name + '](' + board.as_posix() + ')').encode() in document.read_bytes()
    assert board.exists(), 'saving text removed board'
    def test_paint_window():
        for hwnd, _, _ in windows():
            title = c.create_unicode_buffer(256)
            user.GetWindowTextW(hwnd, title, len(title))
            if board_name in title.value and 'Paint' in title.value:
                return hwnd
        return None
    paint = wait_for(test_paint_window, 'external Paint launched for the board')
    user.PostMessageW(paint, 0x10, 0, 0)
    print('PASS: proportional images, hover, scroll/split, edits/Undo/save, freeboard PNG/link and external Paint')
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
