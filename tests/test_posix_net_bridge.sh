#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "$(uname -s)" in
  Linux) ;;
  *)
    echo "PASS: POSIX net bridge tests skipped on non-Linux"
    exit 0
    ;;
esac

BUILD_DIR="$ROOT/build/posix-net-bridge-test"
mkdir -p "$BUILD_DIR"
SRC="$BUILD_DIR/posix_net_bridge_test.c"
BIN="$BUILD_DIR/posix_net_bridge_test"
JS_BRIDGE="$ROOT/js_bridge.c"

cat > "$SRC" <<EOF_C
#include "$JS_BRIDGE"

#include <arpa/inet.h>

static int failures = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("PASS: %s\\n", name);
    } else {
        printf("FAIL: %s\\n", name);
        failures++;
    }
}

static int nonblock_flag(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return (flags & O_NONBLOCK) != 0;
}

int main(void) {
    osc_str localhost = { "localhost", 9 };
    int32_t ip = net_resolve_ipv4(localhost);
    check(ip != 0, "net_resolve_ipv4 resolves localhost");

    int timeout_sock = socket(AF_INET, SOCK_STREAM, 0);
    check(timeout_sock >= 0, "timeout socket created");
    if (timeout_sock >= 0) {
        check(net_set_recv_timeout(timeout_sock, 250) == 0,
              "net_set_recv_timeout succeeds");
        struct timeval recv_tv;
        socklen_t recv_len = (socklen_t)sizeof(recv_tv);
        int recv_ok = getsockopt(timeout_sock, SOL_SOCKET, SO_RCVTIMEO,
                                 &recv_tv, &recv_len);
        check(recv_ok == 0 && recv_tv.tv_sec == 0 && recv_tv.tv_usec > 0,
              "SO_RCVTIMEO applied");
        close(timeout_sock);
    }

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    check(listen_sock >= 0, "listener socket created");
    int port = 0;
    if (listen_sock >= 0) {
        int one = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in listen_addr;
        memset(&listen_addr, 0, sizeof(listen_addr));
        listen_addr.sin_family = AF_INET;
        listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        listen_addr.sin_port = 0;
        check(bind(listen_sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) == 0,
              "listener bind succeeds");
        check(listen(listen_sock, 1) == 0, "listener listen succeeds");
        socklen_t addr_len = (socklen_t)sizeof(listen_addr);
        check(getsockname(listen_sock, (struct sockaddr *)&listen_addr, &addr_len) == 0,
              "listener port discovered");
        port = ntohs(listen_addr.sin_port);
    }

    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    check(client_sock >= 0, "client socket created");
    if (client_sock >= 0 && port > 0) {
        int before_nb = nonblock_flag(client_sock);
        int rc = net_connect_timeout_ipv4(client_sock, htonl(INADDR_LOOPBACK), port, 1000);
        int after_nb = nonblock_flag(client_sock);
        check(rc == 0, "net_connect_timeout_ipv4 connects to local listener");
        check(before_nb == 0 && after_nb == 0,
              "net_connect_timeout_ipv4 restores blocking mode after success");
        int accepted = accept(listen_sock, NULL, NULL);
        check(accepted >= 0, "listener accepts primed connection");
        if (accepted >= 0) close(accepted);
        close(client_sock);
    }
    if (listen_sock >= 0) close(listen_sock);

    int error_sock = socket(AF_INET, SOCK_STREAM, 0);
    check(error_sock >= 0, "error socket created");
    if (error_sock >= 0) {
        int before_nb = nonblock_flag(error_sock);
        int rc = net_connect_timeout_ipv4(error_sock, htonl(INADDR_LOOPBACK), 1, 200);
        int after_nb = nonblock_flag(error_sock);
        check(rc != 0, "net_connect_timeout_ipv4 reports refused/timeout");
        check(before_nb == 0 && after_nb == 0,
              "net_connect_timeout_ipv4 restores blocking mode after error");
        close(error_sock);
    }

    if (failures != 0) return 1;
    return 0;
}
EOF_C

cc -D_GNU_SOURCE -ffunction-sections -fdata-sections -I"$ROOT" \
  "$SRC" -Wl,--gc-sections -o "$BIN"
"$BIN"
