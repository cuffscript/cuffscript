import sys, random, time, os
from harness import *
BIN = sys.argv[1]; N = int(sys.argv[2]); seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
random.seed(seed)

def piece():
    k = random.randint(0, 11)
    if k == 0: return bytes(random.randint(32, 126) for _ in range(random.randint(1, 20)))
    if k == 1: return os.urandom(random.randint(1, 8))                                   # arbitrary bytes
    if k == 2: return "가나다라é😀".encode()[: random.randint(1, 14)]                  # possibly truncated UTF-8
    if k == 3: return b"\x1b[" + bytes(random.choice(b"0123456789;:<=>?") for _ in range(random.randint(0, 40))) + bytes([random.randint(0x40, 0x7e)])
    if k == 4: return b"\x1b[" + b"1" * random.randint(40, 300)                         # oversized/unterminated CSI
    if k == 5: return random.choice([b"\x1b[200~", b"\x1b[201~", b"\x1b[13;2u", b"\x1b[99;5u", b"\x1b[27;2;13~", b"\x1bO", b"\x1b", b"\x1b\x1b"])
    if k == 6: return bytes([random.randint(0, 31)])
    if k == 7: return random.choice([b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D", b"\x1b[H", b"\x1b[F", b"\x1b[3~", b"\x7f", b"\x7f\x7f\x7f"])
    if k == 8: return b"\r"
    if k == 9: return b"\xe2\x80\xae\xc2\x85"                                           # bidi override / C1
    if k == 10: return b"\x1b[200~" + os.urandom(random.randint(1, 60)) + random.choice([b"", b"\x1b[201~"])
    return b" " * random.randint(1, 30)

crashes = 0
for t in range(N):
    s = Session([BIN, "--no-banner", "--timeout", "500"], cols=random.choice([8, 20, 40, 80]), rows=24)
    s.pump(0.35)
    for _ in range(random.randint(5, 30)):
        os.write(s.fd, piece()); s.pump(0.01)
    s.pump(0.3)
    for _ in range(3): s.send(b"\x03", 0.05)
    s.send(b"\x04", 0.1)
    s.send(b":exit\r", 0.3)
    rc = s.finish(1.5)
    out = s.raw.decode("utf-8", "replace")
    bad = ("AddressSanitizer" in out) or ("runtime error" in out) or ("LeakSanitizer" in out) or (rc not in (0, 130))
    if bad:
        crashes += 1
        print(f"--- trial {t} rc={rc}"); print(out[-600:])
        if crashes >= 3: break
print(f"fuzz: {N} trials seed {seed}, problems: {crashes}")
sys.exit(1 if crashes else 0)
