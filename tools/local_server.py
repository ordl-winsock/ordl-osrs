#!/usr/bin/env python3
"""
tools/local_server.py — minimal OSRS rev-239 protocol server (test harness).

Speaks enough of the OSRS game protocol to let the ORDL pure-C client log in,
receive the map rebuild + player info, and walk around — entirely locally, no
Jagex involvement. Used to validate the client's network/ISAAC/GPI stack
end-to-end.

Usage:
  python3 tools/local_server.py [--port 43599]
Then run the client with:
  OSRS_WORLD=127.0.0.1:43599 OSRS_USER=bob OSRS_PASS=hunter2 \
  OSRS_RSA_MODULUS=<printed> OSRS_RSA_EXPONENT=10001 ./build/osrs_client
"""

import argparse
import random
import socket
import struct
import sys
import threading
import time

# --------------------------------------------------------------------------
# RSA keypair (pure-python, Miller-Rabin). 1024-bit.
# --------------------------------------------------------------------------

def _is_probable_prime(n, k=16):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d = n - 1
    r = 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(k):
        a = random.randrange(2, n - 1)
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = (x * x) % n
            if x == n - 1:
                break
        else:
            return False
    return True


def _gen_prime(bits):
    while True:
        c = random.getrandbits(bits) | (1 << (bits - 1)) | 1
        if _is_probable_prime(c):
            return c


def gen_rsa(bits=1024, e=65537):
    while True:
        p = _gen_prime(bits // 2)
        q = _gen_prime(bits // 2)
        if p == q:
            continue
        n = p * q
        phi = (p - 1) * (q - 1)
        if phi % e == 0:
            continue
        d = pow(e, -1, phi)
        return n, e, d


# Hard-coded 1024-bit RSA keypair for reproducible local testing.
_LOCAL_TEST_KEY = {
    "n": 0x9e79ee6be98c6302061d2d36d9dad700c185931fab2d4838ce8eb7e45ef0662249e9677bf19a881efcd313a9d754aa6bd4b47fe36ffc4367625e8c938befcf6eaae871e367c270027e9bb1f9a19d60ac93d169a5a9290328f6202b6eaaaa1885c84eea71ea528a04d0ffc48c88a390d0d1640ce25e73693fcaeb18b3f08e03eb,
    "e": 0x10001,
    "d": 0x2723c228d0c3a568488e482e75dbc0a22529924e8dfe5f09f8a9f2ecea2dc16bd2895ccf95ba2ec84874f1344f444fdad5ebc6722ba448196e881f7c0961aeff25c7ef27c5e8577c3503d8d0a9f2369e07bb283d888177bcdb1f16def38309536150807f364b95ab72d71155d45805ca3373a5c6ffa8dc3e1fa068486a81ac21,
}


def get_local_test_key(bits=1024):
    n, e, d = _LOCAL_TEST_KEY["n"], _LOCAL_TEST_KEY["e"], _LOCAL_TEST_KEY["d"]
    if n.bit_length() == bits:
        return n, e, d
    print(f"[srv] WARNING: hard-coded key is {n.bit_length()} bits, "
          f"requested {bits} bits. Generating fresh keypair...")
    return gen_rsa(bits, e)


# --------------------------------------------------------------------------
# ISAAC stream cipher (Bob Jenkins) — mirrors src/isaac.c exactly.
# --------------------------------------------------------------------------

_MASK = 0xFFFFFFFF


def _isaac_mix(v):
    a, b, c, d, e, f, g, h = v
    a ^= (b << 11) & _MASK; d = (d + a) & _MASK; b = (b + c) & _MASK
    b ^= c >> 2;           e = (e + b) & _MASK; c = (c + d) & _MASK
    c ^= (d << 8) & _MASK; f = (f + c) & _MASK; d = (d + e) & _MASK
    d ^= e >> 16;          g = (g + d) & _MASK; e = (e + f) & _MASK
    e ^= (f << 10) & _MASK;h = (h + e) & _MASK; f = (f + g) & _MASK
    f ^= g >> 4;           a = (a + f) & _MASK; g = (g + h) & _MASK
    g ^= (h << 8) & _MASK; b = (b + g) & _MASK; h = (h + a) & _MASK
    h ^= a >> 9;           c = (c + h) & _MASK; a = (a + b) & _MASK
    return [a, b, c, d, e, f, g, h]


class Isaac:
    WORDS = 256

    def __init__(self, seed_words):
        self.mem = [0] * self.WORDS
        self.rsl = [0] * self.WORDS
        for i, w in enumerate(seed_words[:self.WORDS]):
            self.rsl[i] = w & _MASK
        self.a = self.b = self.c = 0
        v = [0x9E3779B9] * 8
        for _ in range(4):
            v = _isaac_mix(v)
        for i in range(0, self.WORDS, 8):
            for j in range(8):
                v[j] = (v[j] + self.rsl[i + j]) & _MASK
            v = _isaac_mix(v)
            self.mem[i:i + 8] = v
        for i in range(0, self.WORDS, 8):
            for j in range(8):
                v[j] = (v[j] + self.mem[i + j]) & _MASK
            v = _isaac_mix(v)
            self.mem[i:i + 8] = v
        self._refill()
        self.cnt = self.WORDS

    def _refill(self):
        a, b, c = self.a, self.b, self.c
        c = (c + 1) & _MASK
        b = (b + c) & _MASK
        for i in range(self.WORDS):
            x = self.mem[i]
            m = i & 3
            if m == 0:
                a ^= (a << 13) & _MASK
            elif m == 1:
                a ^= (a >> 6)
            elif m == 2:
                a ^= (a << 2) & _MASK
            else:
                a ^= (a >> 16)
            a = (self.mem[(i + 128) & 0xFF] + a) & _MASK
            y = (self.mem[(x >> 2) & 0xFF] + a + b) & _MASK
            self.mem[i] = y
            b = (self.mem[(y >> 10) & 0xFF] + x) & _MASK
            self.rsl[i] = b
        self.a, self.b, self.c = a, b, c

    def next(self):
        if self.cnt == 0:
            self._refill()
            self.cnt = self.WORDS
        self.cnt -= 1
        return self.rsl[self.cnt]


# --------------------------------------------------------------------------
# XTEA (decrypt for login payload) — mirrors src/xtea.c.
# --------------------------------------------------------------------------

def xtea_decrypt(block: bytearray, key):
    # key: list of 4 uint32. block length multiple of 8, decrypted in place.
    for off in range(0, len(block), 8):
        v0 = struct.unpack(">I", block[off:off + 4])[0]
        v1 = struct.unpack(">I", block[off + 4:off + 8])[0]
        total = (0x9E3779B9 * 32) & _MASK
        for _ in range(32):
            v1 = (v1 - ((((v0 << 4) & _MASK) ^ (v0 >> 5)) + v0 ^
                        (total + key[(total >> 11) & 3]))) & _MASK
            total = (total - 0x9E3779B9) & _MASK
            v0 = (v0 - ((((v1 << 4) & _MASK) ^ (v1 >> 5)) + v1 ^
                        (total + key[total & 3]))) & _MASK
        block[off:off + 4] = struct.pack(">I", v0)
        block[off + 4:off + 8] = struct.pack(">I", v1)


# --------------------------------------------------------------------------
# Byte readers matching the client's Jagex primitives.
# --------------------------------------------------------------------------

class Rd:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def g1(self):
        v = self.d[self.p]; self.p += 1; return v

    def g2(self):
        v = (self.d[self.p] << 8) | self.d[self.p + 1]; self.p += 2; return v

    def g4(self):
        v = struct.unpack(">I", self.d[self.p:self.p + 4])[0]; self.p += 4
        return v

    def g8(self):
        v = struct.unpack(">Q", self.d[self.p:self.p + 8])[0]; self.p += 8
        return v

    def gjstr(self):
        s = b""
        while self.p < len(self.d) and self.d[self.p] != 0:
            s += bytes([self.d[self.p]]); self.p += 1
        self.p += 1
        return s.decode("latin1")

    def bytes(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v


class BitWriter:
    def __init__(self):
        self.buf = bytearray(8192)
        self.bitpos = 0

    def bits(self, n, v):
        for i in range(n - 1, -1, -1):
            byte = self.bitpos >> 3
            bit = 7 - (self.bitpos & 7)
            if (v >> i) & 1:
                self.buf[byte] |= (1 << bit)
            self.bitpos += 1

    def bytes_len(self):
        return (self.bitpos + 7) >> 3

    def data(self):
        return bytes(self.buf[:self.bytes_len()])


# --------------------------------------------------------------------------
# Game server.
# --------------------------------------------------------------------------

WORLD_AREA = 0  # unused by client
PLAYER_INDEX = 1


class ClientConn(threading.Thread):
    def __init__(self, sock, addr, rsa):
        super().__init__(daemon=True)
        self.sock = sock
        self.addr = addr
        self.n, self.e, self.d = rsa
        self.session_id = random.getrandbits(64)
        self.px = 3222   # Lumbridge spawn-ish
        self.pz = 3218
        self.level = 0
        self.self_stationary = False
        self.running = True
        self.pow = False

    # -- framing helpers -------------------------------------------------
    def send(self, b):
        self.sock.sendall(b)

    def recv_exact(self, n):
        out = b""
        while len(out) < n:
            chunk = self.sock.recv(n - len(out))
            if not chunk:
                raise ConnectionError("closed")
            out += chunk
        return out

    # -- main flow --------------------------------------------------------
    def run(self):
        try:
            self.handle()
        except (ConnectionError, OSError) as ex:
            print(f"[srv] {self.addr} disconnected: {ex}")
        except Exception as ex:
            import traceback
            traceback.print_exc()
        finally:
            try:
                self.sock.close()
            except OSError:
                pass

    def handle(self):
        # Init handshake: opcode 14
        op = self.recv_exact(1)[0]
        if op != 14:
            print(f"[srv] bad init opcode {op}")
            return
        self.send(bytes([0]) + struct.pack(">Q", self.session_id))

        # GAMELOGIN: opcode 16 + varshort len + block
        op = self.recv_exact(1)[0]
        if op != 16:
            print(f"[srv] bad gamelogin opcode {op}")
            return
        blen = struct.unpack(">H", self.recv_exact(2))[0]
        block = self.recv_exact(blen)
        self.handle_login_block(block)

    def handle_login_block(self, block):
        r = Rd(block)
        revision = r.g4()
        r.g4()  # subVersion
        r.g4()  # serverVersion
        client_type = r.g1()
        platform_type = r.g1()
        r.g1()  # externalAuthenticatorType
        rsa_len = r.g2()
        rsa_ct = r.bytes(rsa_len)
        xtea_payload = bytearray(r.bytes(len(block) - r.p))
        print(f"[srv] login block: rev={revision} ctype={client_type} "
              f"ptype={platform_type} rsa_len={rsa_len}")

        # RSA decrypt
        ct_int = int.from_bytes(rsa_ct, "big")
        pt_int = pow(ct_int, self.d, self.n)
        pt_len = (self.n.bit_length() + 7) // 8
        pt = pt_int.to_bytes(pt_len, "big")
        rr = Rd(pt)
        enc_check = rr.g1()
        seed = [rr.g4() for _ in range(4)]
        sess = rr.g8()
        otp_type = rr.g1()
        rr.g4()
        auth_type = rr.g1()
        password = rr.gjstr()
        kind = "legacy" if auth_type == 0 else ("jagex" if auth_type == 2 else f"?{auth_type}")

        # XTEA decrypt payload
        xtea_decrypt(xtea_payload, seed)
        pr = Rd(bytes(xtea_payload))
        username = pr.gjstr()
        print(f"[srv] login OK for {username!r} ({kind})")

        # ISAAC: server reads client pkts with seed, writes with seed+50
        self.isaac_read = Isaac(seed)
        self.isaac_write = Isaac([(w + 50) & _MASK for w in seed])

        # Optional proof-of-work round (enabled with --pow): issue a SHA256
        # hashcash challenge and require a correct POW_REPLY before login OK.
        if self.pow:
            if not self.do_pow_round():
                print("[srv] PoW failed/incorrect; closing")
                return

        # Login OK response: [2][len][payload]
        payload = bytearray()
        payload.append(0)          # auth flag: none
        payload += b"\x00" * 4
        payload.append(0)          # staff mod level
        payload.append(0)          # player mod
        payload += struct.pack(">H", PLAYER_INDEX)
        payload.append(1)          # members
        payload += struct.pack(">Q", 0)  # account hash
        payload += struct.pack(">Q", 0)  # user id
        payload += struct.pack(">Q", 0)  # user hash
        self.send(bytes([2, len(payload)]) + bytes(payload))
        print(f"[srv] login OK sent; entering game as index {PLAYER_INDEX}")

        self.in_game(username)

    # -- proof of work ------------------------------------------------------
    def do_pow_round(self):
        import hashlib
        version, difficulty = 1, 16
        salt = "deadbeefcafe0123"
        # Jagex framing: [69][len:2][id][version][difficulty][salt(jstr)]
        body = bytes([0, version, difficulty]) + salt.encode() + b"\x00"
        ch = bytes([69]) + struct.pack(">H", len(body)) + body
        self.send(ch)
        # read POW_REPLY: opcode 19 + varshort len + 8-byte result
        hdr = self.recv_exact(3)
        opcode, rlen = hdr[0], struct.unpack(">H", hdr[1:3])[0]
        body = self.recv_exact(rlen)
        result = struct.unpack(">Q", body)[0]
        # verify: sha256(hex(version)+hex(difficulty)+salt+hex(result)) has
        # >= difficulty leading zero bits
        cand = ("%x%x%s%x" % (version, difficulty, salt, result)).encode()
        h = hashlib.sha256(cand).digest()
        bits = 0
        ok = True
        for b in h:
            if b == 0:
                bits += 8
                continue
            for j in range(7, -1, -1):
                if b & (1 << j):
                    ok = False
                    break
                bits += 1
            break
        good = bits >= difficulty
        print(f"[srv] PoW reply result={result:#x} leading_zeros={bits} "
              f"difficulty={difficulty} -> {'OK' if good else 'BAD'}")
        return good and opcode == 19

    # -- in-game ----------------------------------------------------------
    def write_packet(self, opcode, payload=b"", var=None):
        """var: None (fixed 0-len), 'byte', or 'short'."""
        out = bytearray()
        obf = (opcode + (self.isaac_write.next() & 0xFF)) & 0xFF
        out.append(obf)
        if var == "byte":
            out.append(len(payload) & 0xFF)
        elif var == "short":
            out += struct.pack(">H", len(payload))
        out += payload
        self.send(bytes(out))

    def send_rebuild_login(self):
        # GPI init block: 30-bit self coord + 18-bit x 2046 low-res
        bw = BitWriter()
        packed = ((self.level & 3) << 28) | ((self.px & 0x3FFF) << 14) | \
                 (self.pz & 0x3FFF)
        bw.bits(30, packed)
        for i in range(1, 2048):
            if i == PLAYER_INDEX:
                continue
            bw.bits(18, 0)
        gpi = bw.data()
        zone_x = self.px >> 3
        zone_z = self.pz >> 3
        payload = bytearray()
        # 30-bit packed base tile coord (level|x|z) little-endian + worldArea
        packed = ((self.level & 3) << 28) | \
                 ((self.px & 0x3FFF) << 14) | \
                 (self.pz & 0x3FFF)
        payload += struct.pack("<I", packed)
        payload += struct.pack("<H", WORLD_AREA)
        payload += gpi
        self.write_packet(49, bytes(payload), var="short")
        print(f"[srv] sent REBUILD_LOGIN base zone {zone_x},{zone_z} "
              f"pos {self.px},{self.pz}")

    def send_player_info(self, moved, dx, dz, teleport):
        bw = BitWriter()
        # Pass membership uses the PREVIOUS cycle's stationary flag.
        prev_stationary = self.self_stationary

        def emit_self():
            if moved:
                bw.bits(1, 1)  # update
                bw.bits(1, 0)  # no extinfo
                if teleport:
                    bw.bits(2, 3)
                    bw.bits(1, 1)  # large
                    bw.bits(2, 0)
                    bw.bits(14, dx & 0x3FFF)
                    bw.bits(14, dz & 0x3FFF)
                else:
                    bw.bits(2, 1)  # walk
                    bw.bits(3, self._dir_opcode(dx, dz))
            else:
                self._stationary(bw, 1)  # skip run of 1

        # Pass 1: high-res non-stationary
        if not prev_stationary:
            emit_self()
        # Pass 2: high-res stationary
        if prev_stationary:
            emit_self()
        # Pass 3: low-res stationary = none. Pass 4: 2046 low-res idle.
        self._stationary(bw, 2046)
        self.write_packet(28, bw.data(), var="short")
        # Update stationary for next cycle.
        self.self_stationary = not moved

    @staticmethod
    def _dir_opcode(dx, dz):
        table = {(dx, dz): op for op, (dx, dz) in enumerate(
            [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1),
             (1, 1)])}
        return table.get((max(-1, min(1, dx)), max(-1, min(1, dz))), 1)

    @staticmethod
    def _stationary(bw, skips):
        count = skips - 1
        bw.bits(1, 0)
        if count == 0:
            bw.bits(2, 0)
        elif count <= 0x1F:
            bw.bits(2, 1); bw.bits(5, count)
        elif count <= 0xFF:
            bw.bits(2, 2); bw.bits(8, count)
        else:
            bw.bits(2, 3); bw.bits(11, count)

    def read_client_packets(self, target):
        """Read available client->server packets; returns clicked move."""
        self.sock.setblocking(False)
        data = b""
        try:
            while True:
                chunk = self.sock.recv(8192)
                if not chunk:
                    self.running = False
                    break
                data += chunk
        except BlockingIOError:
            pass
        self.sock.setblocking(True)
        r = Rd(data)
        clicked = None
        # client->server packet lengths (must match client's table)
        FIXED = {1: 10, 4: 0, 89: 0, 24: 0, 10: 5}
        VARBYTE = {114, 69, 34}
        while r.p < len(data):
            raw = r.g1()
            opcode = (raw - (self.isaac_read.next() & 0xFF)) & 0xFF
            if opcode == 114:  # MOVE_GAMECLICK (var byte)
                ln = r.g1()
                body = r.bytes(ln)
                x = body[0] | (body[1] << 8)
                z = body[2] | (body[3] << 8)
                clicked = (x, z)
                print(f"[srv] MOVE_GAMECLICK -> {x},{z}")
            elif opcode in FIXED:
                r.bytes(FIXED[opcode])
            elif opcode in VARBYTE:
                ln = r.g1()
                r.bytes(ln)
            else:
                # unknown; can't determine length reliably -> stop parsing
                break
        return clicked

    def in_game(self, username):
        self.send_rebuild_login()
        # a welcome game message (opcode 74, var byte)
        # MESSAGE_GAME format is complex; skip for now.
        ticks = 0
        pending_move = None
        while self.running:
            clicked = self.read_client_packets(0.6)
            if not self.running:
                break
            if clicked is not None:
                pending_move = clicked
            # Apply movement: teleport straight to the clicked tile
            if pending_move is not None:
                tx, tz = pending_move
                dx, dz = tx - self.px, tz - self.pz
                teleport = abs(dx) > 2 or abs(dz) > 2
                if not teleport:
                    # clamp single step
                    dx = max(-1, min(1, dx))
                    dz = max(-1, min(1, dz))
                self.px += dx
                self.pz += dz
                self.send_player_info(True, dx, dz, teleport)
                if teleport or (self.px == tx and self.pz == tz):
                    pending_move = None
            else:
                self.send_player_info(False, 0, 0, False)
            ticks += 1
            time.sleep(0.6)  # ~1 game tick


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=43599)
    ap.add_argument("--bits", type=int, default=1024)
    ap.add_argument("--pow", action="store_true",
                    help="require a proof-of-work round before login OK")
    args = ap.parse_args()

    n, e, d = get_local_test_key(args.bits)
    print(f"[srv] RSA modulus (set OSRS_RSA_MODULUS to this):")
    print(f"  {n:0{args.bits // 4}x}")
    print(f"[srv] exponent: {e:x}")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.port))
    srv.listen(8)
    print(f"[srv] listening on 127.0.0.1:{args.port}")
    print(f"[srv] run client:")
    print(f"  OSRS_WORLD=127.0.0.1:{args.port} OSRS_USER=bob OSRS_PASS=x \\")
    print(f"  OSRS_RSA_MODULUS={n:0{args.bits // 4}x} OSRS_RSA_EXPONENT={e:x} \\")
    print(f"  ./build/osrs_client")

    while True:
        cs, addr = srv.accept()
        print(f"[srv] connection from {addr}")
        conn = ClientConn(cs, addr, (n, e, d))
        conn.pow = args.pow
        conn.start()


if __name__ == "__main__":
    main()
