/*
 * aurora-modem-lpm: keep the modem in low power mode.
 *
 * Nothing in the Halium container puts the LTE modem into low power mode, a
 * job the Android telephony framework does on stock: after boot the modem
 * reports itself as shutting down yet keeps searching for networks, which
 * costs battery on a watch without a cellular stack. GNSS and the Wi-Fi
 * firmware, which also runs on the modem, keep working in low power mode;
 * stock Wear OS uses it when there is no active eSIM.
 *
 * This watches the modem's QMI Device Management Service over QRTR. Whenever
 * the service appears (at boot and after a modem restart) or the operating
 * mode changes, it sets low power mode unless the modem is already in a low
 * power, offline or factory test mode.
 */
#include <errno.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef AF_QIPCRTR
#define AF_QIPCRTR 42
#endif

#define QMI_SERVICE_DMS 2

enum { QMI_REQUEST = 0, QMI_RESPONSE = 2, QMI_INDICATION = 4 };
enum { DMS_EVENT_REPORT = 0x0001, DMS_GET_OPERATING_MODE = 0x002d, DMS_SET_OPERATING_MODE = 0x002e };
enum { TLV_RESULT = 0x02, TLV_OPERATING_MODE = 0x01, TLV_EVENT_OPERATING_MODE = 0x14 };

/* QMI DMS operating modes */
enum {
    MODE_ONLINE = 0, MODE_LOW_POWER = 1, MODE_FACTORY_TEST = 2, MODE_OFFLINE = 3,
    MODE_RESET = 4, MODE_SHUTTING_DOWN = 5, MODE_PERSISTENT_LOW_POWER = 6,
    MODE_MODE_ONLY_LOW_POWER = 7
};

struct qmi_header {
    uint8_t type;
    uint16_t txn;
    uint16_t msg_id;
    uint16_t len;
} __attribute__((packed));

static uint16_t next_txn = 1;

/* Look up the first TLV of the given type in a QMI message body */
static const uint8_t *find_tlv(const uint8_t *body, size_t len, uint8_t type, uint16_t *tlv_len)
{
    size_t off = 0;
    while (off + 3 <= len) {
        uint16_t l = body[off + 1] | body[off + 2] << 8;
        if (off + 3 + l > len)
            break;
        if (body[off] == type) {
            *tlv_len = l;
            return body + off + 3;
        }
        off += 3 + l;
    }
    return NULL;
}

/* Send a request with at most one one-byte TLV and wait for its response;
 * indications received meanwhile are handed to handle_indication(). Returns
 * the response length or -1. */
static void handle_indication(int sock, const struct sockaddr_qrtr *dms, const uint8_t *msg, ssize_t len);

static ssize_t transact(int sock, const struct sockaddr_qrtr *dms, uint16_t msg_id,
                        int tlv_type, uint8_t tlv_value, uint8_t *resp, size_t resp_size)
{
    uint8_t req[sizeof(struct qmi_header) + 4];
    struct qmi_header *hdr = (struct qmi_header *)req;
    size_t len = sizeof *hdr;
    uint16_t txn = next_txn++;

    hdr->type = QMI_REQUEST;
    hdr->txn = txn;
    hdr->msg_id = msg_id;
    if (tlv_type >= 0) {
        req[len++] = tlv_type;
        req[len++] = 1;
        req[len++] = 0;
        req[len++] = tlv_value;
    }
    hdr->len = len - sizeof *hdr;
    if (sendto(sock, req, len, 0, (const struct sockaddr *)dms, sizeof *dms) < 0)
        return -1;

    for (;;) {
        struct sockaddr_qrtr from;
        socklen_t from_len = sizeof from;
        ssize_t n = recvfrom(sock, resp, resp_size, 0, (struct sockaddr *)&from, &from_len);
        const struct qmi_header *r = (const struct qmi_header *)resp;
        if (n < 0)
            return -1;
        if (n < (ssize_t)sizeof *r || from.sq_node != dms->sq_node || from.sq_port != dms->sq_port)
            continue;
        if (r->type == QMI_RESPONSE && r->txn == txn && r->msg_id == msg_id)
            return n;
        if (r->type == QMI_INDICATION)
            handle_indication(sock, dms, resp, n);
    }
}

static int response_ok(const uint8_t *resp, ssize_t len)
{
    uint16_t l;
    const uint8_t *result = find_tlv(resp + sizeof(struct qmi_header),
                                     len - sizeof(struct qmi_header), TLV_RESULT, &l);
    return result && l == 4 && !(result[0] | result[1] << 8);
}

static void ensure_low_power(int sock, const struct sockaddr_qrtr *dms, int mode)
{
    uint8_t resp[512];
    ssize_t n;

    switch (mode) {
    case MODE_LOW_POWER:
    case MODE_PERSISTENT_LOW_POWER:
    case MODE_MODE_ONLY_LOW_POWER:
    case MODE_OFFLINE:
    case MODE_FACTORY_TEST:
    case MODE_RESET:
        return;
    }
    n = transact(sock, dms, DMS_SET_OPERATING_MODE, TLV_OPERATING_MODE, MODE_LOW_POWER, resp, sizeof resp);
    if (n > 0 && response_ok(resp, n))
        fprintf(stderr, "modem operating mode %d -> low power\n", mode);
    else
        fprintf(stderr, "setting low power mode (from %d) failed\n", mode);
}

static void handle_indication(int sock, const struct sockaddr_qrtr *dms, const uint8_t *msg, ssize_t len)
{
    const struct qmi_header *hdr = (const struct qmi_header *)msg;
    const uint8_t *mode;
    uint16_t l;

    if (hdr->msg_id != DMS_EVENT_REPORT)
        return;
    mode = find_tlv(msg + sizeof *hdr, len - sizeof *hdr, TLV_EVENT_OPERATING_MODE, &l);
    if (mode && l >= 1)
        ensure_low_power(sock, dms, mode[0]);
}

/* A DMS server appeared: subscribe to mode changes and apply low power mode */
static void dms_appeared(int sock, const struct sockaddr_qrtr *dms)
{
    uint8_t resp[512];
    const uint8_t *mode;
    uint16_t l;
    ssize_t n;

    n = transact(sock, dms, DMS_EVENT_REPORT, TLV_EVENT_OPERATING_MODE, 1, resp, sizeof resp);
    if (n < 0 || !response_ok(resp, n))
        fprintf(stderr, "subscribing to operating mode changes failed\n");

    n = transact(sock, dms, DMS_GET_OPERATING_MODE, -1, 0, resp, sizeof resp);
    if (n < 0 || !response_ok(resp, n)) {
        fprintf(stderr, "reading the operating mode failed\n");
        return;
    }
    mode = find_tlv(resp + sizeof(struct qmi_header), n - sizeof(struct qmi_header), TLV_OPERATING_MODE, &l);
    if (mode && l >= 1)
        ensure_low_power(sock, dms, mode[0]);
}

int main(void)
{
    struct sockaddr_qrtr local, ctrl, dms = { 0 };
    struct qrtr_ctrl_pkt pkt;
    struct timeval tv = { 5, 0 };
    socklen_t len = sizeof local;
    int ctrl_sock, client = -1;

    setvbuf(stderr, NULL, _IOLBF, 0);
    ctrl_sock = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (ctrl_sock < 0 || getsockname(ctrl_sock, (struct sockaddr *)&local, &len) < 0) {
        perror("qrtr");
        return 1;
    }

    /* the name service reports every DMS server, now and later */
    memset(&pkt, 0, sizeof pkt);
    pkt.cmd = QRTR_TYPE_NEW_LOOKUP;
    pkt.server.service = QMI_SERVICE_DMS;
    ctrl = local;
    ctrl.sq_port = QRTR_PORT_CTRL;
    if (sendto(ctrl_sock, &pkt, sizeof pkt, 0, (struct sockaddr *)&ctrl, sizeof ctrl) < 0) {
        perror("qrtr lookup");
        return 1;
    }

    for (;;) {
        struct pollfd fds[2] = { { ctrl_sock, POLLIN, 0 }, { client, POLLIN, 0 } };
        if (poll(fds, client >= 0 ? 2 : 1, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            return 1;
        }

        if (fds[0].revents & POLLIN) {
            if (recv(ctrl_sock, &pkt, sizeof pkt, 0) == sizeof pkt && pkt.server.service == QMI_SERVICE_DMS) {
                if (pkt.cmd == QRTR_TYPE_NEW_SERVER) {
                    if (client >= 0)
                        close(client);
                    client = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
                    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
                    dms.sq_family = AF_QIPCRTR;
                    dms.sq_node = pkt.server.node;
                    dms.sq_port = pkt.server.port;
                    dms_appeared(client, &dms);
                } else if (pkt.cmd == QRTR_TYPE_DEL_SERVER && client >= 0) {
                    close(client);
                    client = -1;
                }
            }
        }

        if (client >= 0 && (fds[1].revents & POLLIN)) {
            uint8_t msg[512];
            struct sockaddr_qrtr from;
            socklen_t from_len = sizeof from;
            ssize_t n = recvfrom(client, msg, sizeof msg, MSG_DONTWAIT, (struct sockaddr *)&from, &from_len);
            if (n >= (ssize_t)sizeof(struct qmi_header) && ((struct qmi_header *)msg)->type == QMI_INDICATION)
                handle_indication(client, &dms, msg, n);
        }
    }
}
