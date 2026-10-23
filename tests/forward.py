"""检查真实 Darwin 转发核心的双栈 TCP、UDP 和正常停止，不改宿主网络。"""
import ipaddress
import signal
import socket
import struct
import subprocess
import sys
import threading
import time


# 完整读取 SOCKS 控制报文。
def rcv(sock, size):
    data = b""
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise EOFError("short SOCKS request")
        data += part
    return data


# 读取 SOCKS 目标地址和端口。
def dst(sock, atyp):
    size = {1: 4, 4: 16}.get(atyp)
    if size is None:
        raise ValueError(f"unexpected SOCKS address type {atyp}")
    return rcv(sock, size + 2)


# 在回环地址模拟 TCP 和 UDP ASSOCIATE。
def conn(sock, port):
    with sock:
        sock.settimeout(10)
        version, count = rcv(sock, 2)
        assert version == 5 and 0 in rcv(sock, count)
        sock.sendall(b"\x05\x00")
        version, command, reserved, atyp = rcv(sock, 4)
        assert version == 5 and reserved == 0
        dst(sock, atyp)
        sock.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01" + struct.pack("!H", port))
        if command == 1:
            request = b""
            while b"\r\n\r\n" not in request:
                request += rcv(sock, 1)
            assert request.startswith(b"GET /check HTTP/1.1")
            sock.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 14\r\nConnection: close\r\n\r\ntunproxy-check!")
        else:
            assert command == 3
            while sock.recv(1):
                pass


# 接受隔离代理的控制连接。
def acpt(server, port, errors, stop):
    server.settimeout(0.2)
    while not stop.is_set():
        try:
            client, _ = server.accept()
        except socket.timeout:
            continue
        threading.Thread(target=clnt, args=(client, port, errors), daemon=True).start()


# 将代理端异常传回主检查。
def clnt(client, port, errors):
    try:
        conn(client, port)
    except Exception as error:
        errors.append(error)


# 按 SOCKS5 UDP 原样返回目标地址和负载。
def udprly(server, stop):
    server.settimeout(0.2)
    while not stop.is_set():
        try:
            data, peer = server.recvfrom(4096)
        except socket.timeout:
            continue
        assert data[:3] == b"\x00\x00\x00" and data[3] in (1, 4)
        server.sendto(data, peer)


# 计算 IP 和传输层校验和。
def cksum(data):
    if len(data) % 2:
        data += b"\x00"
    value = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return ~value & 65535


# 构造带 Darwin 四字节协议族头的 IP 包。
def pkt(family, proto, payload, seq=0, ack=0, flags=0, target=None):
    src = ipaddress.ip_address("198.19.255.1" if family == 4 else "fd00:7072:6f78::1").packed
    dest = ipaddress.ip_address(target or ("192.0.2.80" if family == 4 else "2001:db8::80")).packed
    if proto == 17:
        segment = struct.pack("!HHHH", 40001, 53, len(payload) + 8, 0) + payload
        offset = 6
    else:
        segment = struct.pack("!HHIIBBHHH", 40002, 80, seq, ack, 80, flags, 65535, 0, 0) + payload
        offset = 16
    pseudo = src + dest + (struct.pack("!BBH", 0, proto, len(segment)) if family == 4 else struct.pack("!I3xB", len(segment), proto))
    checksum = cksum(pseudo + segment) or 65535
    segment = segment[:offset] + struct.pack("!H", checksum) + segment[offset + 2:]
    if family == 4:
        header = struct.pack("!BBHHHBBH4s4s", 69, 0, 20 + len(segment), 1, 0, 64, proto, 0, src, dest)
        header = header[:10] + struct.pack("!H", cksum(header)) + header[12:]
    else:
        header = struct.pack("!IHBB16s16s", 6 << 28, len(segment), proto, 64, src, dest)
    return struct.pack("!I", socket.AF_INET if family == 4 else socket.AF_INET6) + header + segment


# 忽略其他会话的数据并返回指定协议的回复。
def reply(tun, family, proto):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        raw = tun.recv(4096)
        assert struct.unpack("!I", raw[:4])[0] in (socket.AF_INET, socket.AF_INET6)
        data = raw[4:]
        if data[0] >> 4 != family:
            continue
        offset = (data[0] & 15) * 4 if family == 4 else 40
        if data[9 if family == 4 else 6] != proto:
            continue
        return data[offset:]
    raise TimeoutError("no forwarding reply")


# 检查双栈 UDP 回包和 TCP 握手、HTTP 数据。
def flows(tun):
    for family in (4, 6):
        payload = f"tunproxy-udp-{family}".encode()
        tun.send(pkt(family, 17, payload))
        response = reply(tun, family, 17)
        assert struct.unpack("!HH", response[:4]) == (53, 40001)
        assert response[8:] == payload
        tun.send(pkt(family, 6, b"", 100, 0, 2))
        response = reply(tun, family, 6)
        remote, ack = struct.unpack("!II", response[4:12])
        assert response[13] & 18 == 18 and ack == 101
        tun.send(pkt(family, 6, b"", 101, remote + 1, 16))
        request = b"GET /check HTTP/1.1\r\nHost: check\r\n\r\n"
        tun.send(pkt(family, 6, request, 101, remote + 1, 24))
        content = b""
        while b"tunproxy-check!" not in content:
            response = reply(tun, family, 6)
            content += response[(response[12] >> 4) * 4:]
        assert content.startswith(b"HTTP/1.1 200 OK")
        print(f"PASS: IPv{family} UDP, TCP handshake and HTTP payload", flush=True)


# 启动真实核心并确认停止不会挂起。
def main():
    errors = []
    stop = threading.Event()
    with socket.create_server(("127.0.0.1", 0)) as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(("127.0.0.1", 0))
        threading.Thread(target=acpt, args=(tcp, udp.getsockname()[1], errors, stop), daemon=True).start()
        threading.Thread(target=udprly, args=(udp, stop), daemon=True).start()
        tun, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        tun.settimeout(5)
        process = subprocess.Popen(["build/check-forward", str(child.fileno()), "127.0.0.1", str(tcp.getsockname()[1])], pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child.close()
        try:
            flows(tun)
        finally:
            process.send_signal(signal.SIGTERM)
            output, error = process.communicate(timeout=5)
            stop.set()
            tun.close()
            assert process.returncode == 0, (process.returncode, output, error)
        assert not errors, errors
    print("PASS: graceful forwarder shutdown")


# 经实际尾网上游检查 UDP DNS 和 TCP HTTP。
def actual(host, port):
    tun, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    tun.settimeout(10)
    process = subprocess.Popen(["build/check-forward", str(child.fileno()), host, port], pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    child.close()
    try:
        query = struct.pack("!6H", 4321, 256, 1, 0, 0, 0) + b"\x07example\x03com\x00\x00\x01\x00\x01"
        tun.send(pkt(4, 17, query, target="1.1.1.1"))
        answer = reply(tun, 4, 17)[8:]
        ident, flags, questions, count = struct.unpack("!4H", answer[:8])
        assert ident == 4321 and flags & 32768 and flags & 15 == 0 and count > 0
        print("PASS: actual Tailscale SOCKS5 UDP DNS response", flush=True)
        tun.send(pkt(4, 6, b"", 100, 0, 2, target="1.1.1.1"))
        response = reply(tun, 4, 6)
        remote, ack = struct.unpack("!II", response[4:12])
        assert response[13] & 18 == 18 and ack == 101
        tun.send(pkt(4, 6, b"", 101, remote + 1, 16, target="1.1.1.1"))
        request = b"GET / HTTP/1.1\r\nHost: one.one.one.one\r\nConnection: close\r\n\r\n"
        tun.send(pkt(4, 6, request, 101, remote + 1, 24, target="1.1.1.1"))
        content = b""
        while b"\r\n" not in content:
            response = reply(tun, 4, 6)
            content += response[(response[12] >> 4) * 4:]
        assert content.startswith(b"HTTP/")
        print("PASS: actual Tailscale SOCKS5 TCP HTTP response", flush=True)
    finally:
        process.send_signal(signal.SIGTERM)
        output, error = process.communicate(timeout=5)
        tun.close()
        assert process.returncode == 0, (process.returncode, output, error)


if __name__ == "__main__":
    if len(sys.argv) == 3:
        actual(sys.argv[1], sys.argv[2])
    else:
        main()
