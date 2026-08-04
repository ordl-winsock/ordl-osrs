#!/usr/bin/env python3
"""
tools/login_relay.py — TCP relay that captures the OSRS login block for analysis.
Run with sudo after adding a hosts override pointing a world at 127.0.0.1.

  sudo python3 tools/login_relay.py --listen 43594 --target oldschool1.runescape.com:43594

It dumps the client's init (14) and GAMELOGIN (16) bytes to /tmp/relay_capture.bin
so the login block can be compared byte-for-byte with the from-scratch client.
"""
import argparse
import socket
import socketserver
import sys
import threading

CAP = open("/tmp/relay_capture.bin", "wb")
CAP_SC = open("/tmp/relay_capture_sc.bin", "wb")


class Relay(socketserver.BaseRequestHandler):
    def handle(self):
        target_host, target_port = self.server.target
        try:
            upstream = socket.create_connection((target_host, target_port), timeout=10)
        except OSError as e:
            print(f"[relay] upstream connect failed: {e}")
            return
        print(f"[relay] client {self.client_address} -> {target_host}:{target_port}")
        self.request.settimeout(None)
        upstream.settimeout(None)
        state = {"sent": 0, "recv": 0}
        # server->client captured to a separate file
        threading.Thread(target=self.pipe, args=(upstream, self.request, "sc", state), daemon=True).start()
        self.pipe(self.request, upstream, "cs", state)

    def pipe(self, src, dst, direction, state):
        out = CAP if direction == "cs" else CAP_SC
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                out.write(data)
                out.flush()
                if direction == "cs":
                    state["sent"] += len(data)
                    print(f"[relay] C->S {len(data)}B first={data[:1].hex()}")
                else:
                    state["recv"] += len(data)
                    print(f"[relay] S->C {len(data)}B first={data[:1].hex()}")
                dst.sendall(data)
        except OSError:
            pass
        finally:
            try:
                dst.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    address_family = socket.AF_INET6

    def server_bind(self):
        # Dual-stack: accept both IPv4 and IPv6 on same socket
        self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        super().server_bind()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", type=int, default=43594)
    ap.add_argument("--target", default="oldschool1.runescape.com:43594")
    args = ap.parse_args()
    host, _, port = args.target.rpartition(":")
    Server.target = (host, int(port))
    srv = Server(("::", args.listen), Relay)
    print(f"[relay] listening on [::]:{args.listen} (dual-stack) -> {args.target}")
    print(f"[relay] add to /etc/hosts:  127.0.0.1 {host}")
    srv.serve_forever()


if __name__ == "__main__":
    main()
