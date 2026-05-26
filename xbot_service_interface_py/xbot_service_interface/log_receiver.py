"""
xbot-log — receive and display xBot remote log messages.

Services call startRemoteLogging() which multicasts XbotHeader+text to
LOG_MULTICAST_ADDR:MULTICAST_PORT. arg1 carries the ulog severity level.
"""
import argparse
import socket
import struct
import sys
from datetime import datetime

from .datatypes import (
    HEADER_FORMAT, HEADER_SIZE, MessageType, LogLevel,
    LOG_MULTICAST_ADDR, MULTICAST_PORT,
)

# ANSI colours keyed by LogLevel
_RESET = '\033[0m'
_LEVEL_FMT: dict[int, tuple[str, str]] = {
    LogLevel.TRACE:    ('\033[2m',          'TRC'),
    LogLevel.DEBUG:    ('\033[36m',          'DBG'),
    LogLevel.INFO:     ('\033[32m',          'INF'),
    LogLevel.WARNING:  ('\033[33m',          'WRN'),
    LogLevel.ERROR:    ('\033[31m',          'ERR'),
    LogLevel.CRITICAL: ('\033[1;31m',        'CRT'),
    LogLevel.ALWAYS:   ('\033[1m',           'ALW'),
}


def _colour(level: int) -> tuple[str, str]:
    return _LEVEL_FMT.get(level, ('', f'L{level:02d}'))


def _make_socket(bind_ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        pass
    sock.bind((LOG_MULTICAST_ADDR, MULTICAST_PORT))
    iface = bind_ip if bind_ip != '0.0.0.0' else '0.0.0.0'
    mreq = struct.pack('4s4s',
                       socket.inet_aton(LOG_MULTICAST_ADDR),
                       socket.inet_aton(iface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    sock.settimeout(1.0)
    return sock


def _recv_loop(sock: socket.socket, min_level: int, no_colour: bool) -> None:
    while True:
        try:
            data, (src_ip, _) = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            break

        if len(data) < HEADER_SIZE:
            continue

        fields = struct.unpack_from(HEADER_FORMAT, data)
        msg_type   = fields[1]
        level      = fields[5]   # arg1
        service_id = fields[4]
        pay_size   = fields[10]

        if msg_type != MessageType.LOG:
            continue
        if level < min_level:
            continue

        payload = data[HEADER_SIZE: HEADER_SIZE + pay_size]
        try:
            text = payload.decode('utf-8', errors='replace').rstrip('\0')
        except Exception:
            text = repr(payload)

        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        ansi, tag = _colour(level)

        if no_colour:
            print(f"{ts} [{tag}] {src_ip} | {text}", flush=True)
        else:
            print(f"{ansi}{ts} [{tag}]{_RESET} \033[2m{src_ip}\033[0m | {ansi}{text}{_RESET}",
                  flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Receive and display xBot remote log messages')
    parser.add_argument('--bind', '-b', default='0.0.0.0', metavar='IP',
                        help='local interface IP to join multicast on (default: 0.0.0.0)')
    parser.add_argument('--level', '-l',
                        choices=['trace', 'debug', 'info', 'warning', 'error', 'critical'],
                        default='debug',
                        help='minimum log level to display (default: debug)')
    parser.add_argument('--no-colour', '--no-color', action='store_true',
                        help='disable ANSI colour output')
    args = parser.parse_args()

    level_map = {
        'trace':    LogLevel.TRACE,
        'debug':    LogLevel.DEBUG,
        'info':     LogLevel.INFO,
        'warning':  LogLevel.WARNING,
        'error':    LogLevel.ERROR,
        'critical': LogLevel.CRITICAL,
    }
    min_level = level_map[args.level]
    no_colour = args.no_colour or not sys.stdout.isatty()

    sock = _make_socket(args.bind)
    print(f"Listening for xBot log messages on {LOG_MULTICAST_ADDR}:{MULTICAST_PORT}"
          f"  (bind={args.bind}, min_level={args.level.upper()})",
          flush=True)
    try:
        _recv_loop(sock, min_level, no_colour)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == '__main__':
    main()
