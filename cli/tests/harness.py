import os, pty, select, time, signal, struct, fcntl, termios
import pyte

import re, wcwidth

class WideAwareScreen(pyte.Screen):
    """pyte writes a double-width glyph into the last column instead of wrapping it
    to the next row the way xterm, VTE, Terminal.app and Windows Terminal do."""
    def draw(self, data):
        for ch in data:
            if wcwidth.wcwidth(ch) == 2 and self.cursor.x == self.columns - 1:
                self.cursor.x = self.columns   # makes pyte perform its normal wrap first
            super().draw(ch)

# Mode-setting sequences pyte doesn't implement (it would print them as text).
_STRIP = re.compile(rb"\x1b\[[<>]1u|\x1b\[\?u|\x1b\[c")

class Session:
    """Test harness for cuffsh. Runs a program on a real pty with a given window size and feeds everything it
    prints into a pyte terminal emulator, so tests can assert on what a user would
    actually SEE (screen text + cursor), not just on raw escape bytes."""
    def __init__(self, argv, cols=80, rows=24, env_extra=None, stdin_tty=True, kitty=True, answer_queries=True):
        self.kitty, self.answer_queries = kitty, answer_queries
        self.cols, self.rows = cols, rows
        self.screen = WideAwareScreen(cols, rows)
        self.stream = pyte.ByteStream(self.screen)
        self.raw = b""
        env = dict(os.environ); env["TERM"] = "xterm-256color"
        if env_extra: env.update(env_extra)
        pid, fd = pty.fork()
        if pid == 0:
            for k, v in env.items(): os.environ[k] = v
            try: os.execvp(argv[0], argv)
            except Exception as e:
                os.write(2, f"exec failed: {e}\n".encode()); os._exit(127)
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.rc = None

    def pump(self, seconds=0.25):
        end = time.time() + seconds
        while True:
            left = end - time.time()
            if left <= 0: break
            try: r, _, _ = select.select([self.fd], [], [], min(0.05, left))
            except OSError: break
            if self.fd in r:
                try: chunk = os.read(self.fd, 65536)
                except OSError:
                    self._poll(0.5); return          # pty closed: the child is exiting
                if not chunk: break
                self.raw += chunk
                if self.answer_queries:
                    if b"\x1b[?u" in chunk and self.kitty:
                        os.write(self.fd, b"\x1b[?0u")
                    if b"\x1b[c" in chunk:
                        os.write(self.fd, b"\x1b[?62;c")
                self.stream.feed(_STRIP.sub(b"", chunk))
        self._poll()

    def _poll(self, wait_exit=0.0):
        end = time.time() + wait_exit
        while True:
            self._poll_once()
            if self.rc is not None or time.time() >= end: break
            time.sleep(0.02)

    def _poll_once(self):
        if self.rc is None:
            try: wpid, st = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError: wpid, st = self.pid, 0
            if wpid == self.pid:
                self.rc = os.WEXITSTATUS(st) if os.WIFEXITED(st) else -os.WTERMSIG(st)

    def send(self, data, wait=0.15):
        if isinstance(data, str): data = data.encode()
        os.write(self.fd, data); self.pump(wait)

    def text(self):
        return [l.rstrip() for l in self.screen.display]

    def nonblank(self):
        t = self.text()
        while t and not t[-1]: t.pop()
        return t

    def cursor(self): return (self.screen.cursor.y, self.screen.cursor.x)

    def finish(self, wait=1.0):
        self.pump(wait)
        if self.rc is None:
            try: os.kill(self.pid, signal.SIGKILL)
            except ProcessLookupError: pass
            try:
                _, st = os.waitpid(self.pid, 0)
                self.rc = os.WEXITSTATUS(st) if os.WIFEXITED(st) else -os.WTERMSIG(st)
            except ChildProcessError: pass
        try: os.close(self.fd)
        except OSError: pass
        return self.rc

SHIFT_ENTER = b'\x1b[13;2u'
UP, DOWN, RIGHT, LEFT = b'\x1b[A', b'\x1b[B', b'\x1b[C', b'\x1b[D'
