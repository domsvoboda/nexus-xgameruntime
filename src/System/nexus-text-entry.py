#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Native text entry for Wine's GDK implementation; game text stays off argv/disk."""
import argparse
from dataclasses import dataclass
import os
import socket
import struct
import sys

MAGIC = 0x3345544E
CANCELLED = 0x800704C7
INVALID_ARGUMENT = 0x80070057
UNAVAILABLE = 0x80004005
MAX_FIELD_UNITS = 2_097_152
HEADER = struct.Struct('<9I')
RESULT = struct.Struct('<3I')


def clean_host_environment():
    """Do not load Proton's older GLib/GIO into the host GTK process."""
    if os.environ.get('NEXUS_NATIVE_TEXT_CLEAN') == '1':
        return
    clean = dict(os.environ)
    for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'GI_TYPELIB_PATH', 'GIO_MODULE_DIR',
                'GSETTINGS_SCHEMA_DIR', 'GTK_PATH', 'GTK_EXE_PREFIX', 'GTK_DATA_PREFIX',
                'PYTHONPATH', 'PYTHONHOME'):
        clean.pop(key, None)
    for key in list(clean):
        if key.startswith(('XODUS_', 'XUSER_', 'NEXUS_')) and key not in ('NEXUS_NATIVE_TEXT_TOKEN', 'NEXUS_NATIVE_TEXT_CLEAN'):
            clean.pop(key, None)
    clean['PATH'] = '/usr/bin:/bin'
    clean['NEXUS_NATIVE_TEXT_CLEAN'] = '1'
    os.execve('/usr/bin/python3', ['/usr/bin/python3', __file__, *sys.argv[1:]], clean)


def read_exact(stream, size):
    result = bytearray()
    while len(result) < size:
        chunk = stream.read(size - len(result))
        if not chunk:
            raise EOFError('Incomplete request')
        result.extend(chunk)
    return bytes(result)


@dataclass(frozen=True)
class Request:
    title: str
    description: str
    initial: str
    limit: int
    numeric: bool
    password: bool

    def sanitize(self, text):
        if self.numeric:
            text = ''.join(c for c in text if '0' <= c <= '9')
        return text[:self.limit]


def read_request(stream):
    magic, limit, numeric, password, title_size, description_size, initial_size, reserved1, reserved2 = HEADER.unpack(read_exact(stream, HEADER.size))
    sizes = (title_size, description_size, initial_size)
    if magic != MAGIC or not 0 < limit <= MAX_FIELD_UNITS or numeric not in (0, 1) or password not in (0, 1):
        raise ValueError('Invalid request header')
    if any(n > MAX_FIELD_UNITS for n in sizes) or reserved1 or reserved2:
        raise ValueError('Request exceeds protocol bounds')
    fields = [read_exact(stream, size * 2).decode('utf-16-le') for size in sizes]
    if any('\0' in field for field in fields):
        raise ValueError('Embedded terminator')
    return Request(*fields, limit, bool(numeric), bool(password))


def write_result(stream, status, text=''):
    payload = text.encode('utf-8') + b'\0' if status == 0 else b''
    if len(payload) > MAX_FIELD_UNITS * 4 + 1:
        status, payload = INVALID_ARGUMENT, b''
    stream.write(RESULT.pack(MAGIC, status, len(payload)) + payload)
    stream.flush()


def show_dialog(request, channel, writer):
    import gi
    gi.require_version('Gtk', '4.0')
    from gi.repository import Gtk, Gdk, Gio, GLib

    class TextEntry(Gtk.Application):
        sent = False
        adjusting = False

        def do_activate(self):
            self.window = Gtk.ApplicationWindow(application=self, title='Game text entry - ' + (request.title or 'Enter text'))
            self.window.set_default_size(600, 190)
            self.window.set_resizable(False)
            self.window.set_modal(True)
            self.window.add_css_class('game-text-entry')
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
            box.set_margin_top(20)
            box.set_margin_bottom(20)
            box.set_margin_start(20)
            box.set_margin_end(20)
            label = Gtk.Label(label=request.description or request.title or 'Enter text', xalign=0)
            label.set_use_markup(False)
            label.set_wrap(True)
            box.append(label)
            self.entry = Gtk.Entry()
            self.entry.set_visibility(not request.password)
            self.entry.set_input_purpose(Gtk.InputPurpose.DIGITS if request.numeric else Gtk.InputPurpose.FREE_FORM)
            self.entry.set_text(request.sanitize(request.initial))
            self.entry.connect('changed', self.changed)
            self.entry.connect('activate', lambda *_: self.finish(0))
            box.append(self.entry)
            buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12, halign=Gtk.Align.END)
            cancel = Gtk.Button(label='Cancel')
            cancel.connect('clicked', lambda *_: self.finish(CANCELLED))
            ok = Gtk.Button(label='OK')
            ok.add_css_class('suggested-action')
            ok.connect('clicked', lambda *_: self.finish(0))
            buttons.append(cancel)
            buttons.append(ok)
            box.append(buttons)
            self.window.set_child(box)
            self.window.set_default_widget(ok)
            self.window.connect('close-request', lambda *_: self.finish(CANCELLED))
            keys = Gtk.EventControllerKey()
            keys.connect('key-pressed', self.key_pressed)
            self.window.add_controller(keys)
            css = Gtk.CssProvider()
            css.load_from_data(b'.game-text-entry {background:#f7f7f7;color:#111;} .game-text-entry entry, .game-text-entry entry text {background:white;color:#111;caret-color:#111;}')
            Gtk.StyleContext.add_provider_for_display(self.window.get_display(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
            GLib.timeout_add(300, self.peer_alive)
            self.window.present()
            self.entry.grab_focus()
            self.entry.select_region(0, -1)

        def changed(self, entry):
            if self.adjusting:
                return
            text = entry.get_text()
            clean = request.sanitize(text)
            if clean != text:
                position = max(0, entry.get_position())
                self.adjusting = True
                entry.set_text(clean)
                entry.set_position(len(request.sanitize(text[:position])))
                self.adjusting = False

        def key_pressed(self, controller, keyval, keycode, state):
            if keyval == Gdk.KEY_Escape:
                self.finish(CANCELLED)
                return True
            return False

        def peer_alive(self):
            try:
                pending = channel.recv(1, socket.MSG_PEEK | socket.MSG_DONTWAIT)
                if not pending:
                    self.sent = True
                    self.quit()
                    return False
            except BlockingIOError:
                pass
            except OSError:
                self.sent = True
                self.quit()
                return False
            return True

        def finish(self, status):
            if not self.sent:
                self.sent = True
                try:
                    write_result(writer, status, request.sanitize(self.entry.get_text()))
                except OSError:
                    pass
            self.quit()
            return True

    app = TextEntry(application_id='io.github.nexus.TextEntry', flags=Gio.ApplicationFlags.NON_UNIQUE)
    app.run([])
    if not app.sent:
        write_result(writer, CANCELLED)


def main():
    clean_host_environment()
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, required=True)
    options = parser.parse_args()
    if not 0 < options.port < 65536:
        parser.error('Invalid port')
    try:
        token = bytes.fromhex(os.environ.pop('NEXUS_NATIVE_TEXT_TOKEN', ''))
    except ValueError:
        parser.error('Invalid authentication token')
    if len(token) != 32:
        parser.error('Missing private authentication channel')
    with socket.create_connection(('127.0.0.1', options.port), timeout=10) as channel:
        channel.settimeout(None)
        channel.sendall(token)
        with channel.makefile('rb') as reader, channel.makefile('wb') as writer:
            try:
                request = read_request(reader)
                show_dialog(request, channel, writer)
            except (ValueError, UnicodeError):
                write_result(writer, INVALID_ARGUMENT)
            except Exception as error:
                # Never include exception text, which can contain request strings.
                print('Native text entry unavailable: ' + type(error).__name__, file=sys.stderr)
                try:
                    write_result(writer, UNAVAILABLE)
                except OSError:
                    pass


if __name__ == '__main__':
    main()
