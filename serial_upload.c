/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#ifndef WIN32
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <arpa/inet.h>
#else
#include <windows.h>
#include <winsock.h>
#endif
#include <assert.h>

#include "serial_upload.h"
#include "crc/crc16.h"
#include "base64/base64.h"

const char *cmdname;

#define TXBUF_SZ 2100
#define FIRST_SEG_TMO 16
#define NEXT_SEG_TMO 1

struct upload_state {
    const char *devname;
    int speed;
    HANDLE port;
    const char *filename;
    size_t file_sz;
    uint8_t *file;
    int imgchunk;
    int verbose;
} state;

void
dump_hex(const char *hdr, void *bufv, int cnt)
{
    int i;
    char *buf = bufv;

    fprintf(stdout, "%s (%d bytes)\n", hdr, cnt);
    for (i = 0; i < cnt; i++) {
        fprintf(stdout, "%2.2x ", 0xff & buf[i]);
        if (i % 16 == 15) {
            fprintf(stdout, "\n");
        }
    }
    if (i % 16) {
        fprintf(stdout, "\n");
    }
}

#define SHELL_NLIP_PKT          0x0609
#define SHELL_NLIP_DATA         0x0414
#define SHELL_NLIP_MAX_FRAME    128

static int
port_write(HANDLE fd, uint8_t *buf, size_t len)
{
    uint16_t crc;
    size_t off = 0;
    size_t boff;
    size_t blen;
    char tmpbuf[512];
    uint8_t first_b[3];

    crc = crc16_ccitt(CRC16_INITIAL_CRC, buf, len);
    crc = htons(crc);
    memcpy(buf + len, &crc, sizeof(crc));
    len += sizeof(uint16_t);

    if (state.verbose > 1) {
        dump_hex("TX unencoded", buf, len);
    }
    while (off < len) {
        if (off == 0) {
            ((unsigned short *)tmpbuf)[0] = htons(SHELL_NLIP_PKT);
            ((unsigned short *)first_b)[0] = htons(len);
            first_b[2] = buf[0];
            boff = 2;
            off = 1;
            boff += base64_encode(first_b, 3, &tmpbuf[2], 0);
            blen = 90;
        } else {
            ((unsigned short *)tmpbuf)[0] = htons(SHELL_NLIP_DATA);
            boff = 2;
            blen = 93;
        }

        if (blen > len - off) {
            blen = len - off;
        }
        boff += base64_encode(&buf[off], blen, &tmpbuf[boff], 1);
        off += blen;
        tmpbuf[boff++] = '\n';

        if (state.verbose > 1) {
            dump_hex("TX encoded", tmpbuf, boff);
        }
	if (port_write_data(fd, tmpbuf, boff) < 0) {
            return -1;
        }
    }

    return 0;
}

static int
port_read_pkt_len(char *buf, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            return i + 1;
        }
    }
    return 0;
}

static int
port_read(HANDLE fd, uint8_t *buf, size_t maxlen, int tmo)
{
    int end_time, now;
    char tmpbuf[512];
    int rc;
    int off;
    int len;
    int soff;

    now = time_get();
    end_time = now + tmo;

    soff = 0;
    off = 0;
    while (1) {
        if (soff == off) {
            soff = off = 0;
        }
        rc = port_read_poll(fd, &tmpbuf[off], maxlen - off, end_time,
                            state.verbose);
        if (rc < 0) {
            break;
        }
        if (off == 0) {
            while (rc > 0 && (tmpbuf[0] == '\r' || tmpbuf[0] == '\n')) {
                memmove(tmpbuf, &tmpbuf[1], rc - 1);
                rc = rc - 1;
            }
        }
        off += rc;
        while ((len = port_read_pkt_len(&tmpbuf[soff], off - soff))) {
            if (len > 2) {
                assert(soff + len < maxlen);
                if (*((unsigned short *)&tmpbuf[soff]) ==
                  htons(SHELL_NLIP_PKT)) {

                    tmpbuf[soff + len] = '\0';
                    rc = base64_decode(&tmpbuf[soff + 2], &tmpbuf[soff + 2]);
                    rc -= sizeof(unsigned short);
                    if (rc < 0) {
                        return -4;
                    }
                    if (rc != htons(*(unsigned short *)&tmpbuf[soff + 2])) {
                        return -5;
                    }
                    memcpy(buf, &tmpbuf[soff + 4], rc);
                    if (serial_uploader_is_rsp(buf, rc)) {
                        return rc;
                    }
                }
            }
            soff += len;
        }
    }

    return rc;
}

static void
flush_dev_console(void)
{
    port_write_data(state.port, "\n", 1);
}

static int
echo_ctl(int val)
{
    uint8_t buf[512];
    size_t cnt;
    int rc;

    cnt = serial_uploader_echo_ctl(buf, sizeof(buf), 0);
    if (cnt < 0) {
        fprintf(stderr, "%s: message encoding issue %zu\n", cmdname, cnt);
        return (int)cnt;
    }
    rc = port_write(state.port, buf, cnt);
    if (rc < 0) {
        fprintf(stderr, "write fail %d\n", rc);
        return rc;
    }
    rc = port_read(state.port, buf, sizeof(buf), 2);
    if (rc < 0) {
        fprintf(stderr, "read fail %d\n", rc);
        return rc;
    }
    return 0;
}

static size_t
img_upload_tx_prepare(uint8_t *txbuf, size_t off, int *lenp)
{
    size_t blen;
    size_t cnt;

    if (off == 0) {
        blen = 32;
        cnt = serial_uploader_create_seg0(txbuf, TXBUF_SZ, state.file_sz,
          &state.file[off], blen);
    } else {
        blen = state.file_sz - off;
        if (blen > state.imgchunk) {
            blen = state.imgchunk;
        }
        cnt = serial_uploader_create_segX(txbuf, TXBUF_SZ, off,
          &state.file[off], blen);
    }
    if (cnt < 0) {
        fprintf(stderr, "%s: message encoding issue %zd\n",
                cmdname, cnt);
    } else {
        if (state.verbose) {
            fprintf(stdout, " %zu-%zu\n", off, off + blen);
        }
    }
    *lenp = blen;
    return cnt;
}

static int
img_upload(void)
{
    int blen;
    int nblen;
    uint8_t txbuf[TXBUF_SZ];
    int txcnt;
    uint8_t rxbuf[128];
    int rxcnt;
    int tmo;
    int rc;
    size_t off;
    size_t next_off;

    /*
     * Data is base64 encoded. Leave 16 bytes for rest of the CBOR payload.
     * CBOR has [ 'off':<number> 'data':<imgchunk> ]
     */
    state.imgchunk = ((state.imgchunk * 3 / 4) - 16);
    if (state.verbose) {
        fprintf(stdout, "Starting upload %zu bytes\n", state.file_sz);
    }

    txcnt = img_upload_tx_prepare(txbuf, 0, &blen);
    tmo = FIRST_SEG_TMO;
    for (off = 0; off < state.file_sz;) {
        rc = port_write(state.port, txbuf, txcnt);
        if (rc < 0) {
            fprintf(stderr, "write fail %d\n", rc);
            return rc;
        }
        txcnt = img_upload_tx_prepare(txbuf, off + blen, &nblen);
        rxcnt = port_read(state.port, rxbuf, sizeof(rxbuf), tmo);
        if (rxcnt == -14) {
            goto retransmit;
        }
        if (rxcnt < 0) {
            fprintf(stderr, "read fail %d\n", rxcnt);
            return rxcnt;
        }
        rc = serial_uploader_decode_rsp(rxbuf, rxcnt, &next_off);
        if (rc < 0) {
            fprintf(stderr, "%s: response decoding issue %d\n",
              cmdname, rc);
            return rc;
        } else if (rc > 0) {
            fprintf(stderr, "%s: newtmgr error response %d\n",
              cmdname, rc);
            return 5;
        }
	if (state.verbose) {
            fprintf(stdout, "ack to %zu\n", next_off);
	} else {
            fprintf(stdout, ".");
            fflush(stdout);
        }
        if (next_off == state.file_sz) {
            break;
        }
        if (next_off > state.file_sz) {
            fprintf(stderr, "%s: offset %zu larger than file %zu\n",
              cmdname, next_off, state.file_sz);
            return -1;
        }
        tmo = NEXT_SEG_TMO;
        if (off + blen != next_off) {
retransmit:
            txcnt = img_upload_tx_prepare(txbuf, off, &blen);
            if (off == 0) {
                tmo = FIRST_SEG_TMO;
            }
        } else {
            off = next_off;
            blen = nblen;
        }
    }
    if (state.verbose) {
        fprintf(stdout, "Upload complete\n");
    } else {
        fprintf(stdout, "\n");
    }
    return 0;
}

static int
reset_device(void)
{
    uint8_t buf[512];
    size_t cnt;
    int rc;

    cnt = serial_uploader_reset(buf, sizeof(buf), 0);
    if (cnt < 0) {
        fprintf(stderr, "%s: message encoding issue %zu\n", cmdname, cnt);
        return (int)cnt;
    }
    rc = port_write(state.port, buf, cnt);
    if (rc < 0) {
        fprintf(stderr, "write fail %d\n", rc);
        return rc;
    }
    rc = port_read(state.port, buf, sizeof(buf), 2);
    if (rc < 0) {
        fprintf(stderr, "read fail %d\n", rc);
        return rc;
    }
    if (state.verbose) {
        fprintf(stdout, "Device reset\n");
    }
    return 0;
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n%s <options>\n", cmdname);
    fprintf(stderr, "  Options:\n");
    fprintf(stderr, "   -f <filename>      - image file to upload\n");
    fprintf(stderr, "   -d <serialdevname> - serial console for device\n");
    fprintf(stderr, "  [-c <chunk>]        - Max image chunk size (default: 512)\n");
    fprintf(stderr, "  [-s <speed>]        - serial port speed (default: 115200)\n");
    fprintf(stderr, "  [-v]                - verbose output\n");
    exit(1);
}

static char *
parse_opts_optarg(int *argc, char ***argv)
{
    char *arg;

    if (!*argc) {
        usage();
    }
    (*argc)--;
    arg = **argv;
    (*argv)++;

    return arg;
}

static void
parse_opts(int argc, char **argv)
{
    char opt;
    char *arg;
    char *eptr;

    argc--;
    argv++;

    while (argc > 0) {
        if (argv[0][0] != '-') {
            usage();
        }
        opt = argv[0][1];
        if (opt == '\0' || argv[0][2]) {
            usage();
        }
        argc--;
        argv++;

        switch (opt) {
        case 'v':
            state.verbose++;
            break;
        case 'd':
            if (argc < 1) {
                usage();
            }
            state.devname = parse_opts_optarg(&argc, &argv);
            break;
        case 'f':
            if (argc < 1) {
                usage();
            }
            state.filename = parse_opts_optarg(&argc, &argv);
            break;
        case 'c':
            if (argc < 1) {
                usage();
            }
            arg = parse_opts_optarg(&argc, &argv);
            state.imgchunk = strtoul(arg, &eptr, 0);
            if (*eptr != '\0') {
                fprintf(stderr, "%s: Invalid chunk size %s\n",
                  cmdname, arg);
                usage();
            }
            break;
        case 's':
            if (argc < 1) {
                usage();
            }
            arg = parse_opts_optarg(&argc, &argv);
            state.speed = strtoul(arg, &eptr, 0);
            if (*eptr != '\0') {
                fprintf(stderr, "%s: Invalid MTU %s\n", cmdname, arg);
                usage();
            }
            break;
        case 'h':
        case '?':
        default:
            usage();
            break;
        }
    }
}

static void
validate_opts(void)
{
    if (state.imgchunk < 64 || state.imgchunk > 2048) {
        fprintf(stderr, "%s: Invalid image chunk size %d\n",
          cmdname, state.imgchunk);
        fprintf(stderr, "  has to be between 64 and 2048 bytes\n");
        usage();
    }
    switch (state.speed) {
    case 115200:
    case 230400:
    case 921600:
    case 1000000:
        break;
    default:
        fprintf(stderr, "%s: Invalid serial port speed %d\n",
          cmdname, state.speed);
        usage();
    }
    if (state.filename == NULL) {
        fprintf(stderr, "%s: Need file to upload\n", cmdname);
        usage();
    }
    if (state.devname == NULL) {
        fprintf(stderr, "%s: Need serial device to use\n", cmdname);
        usage();
    }
}

int
main(int argc, char **argv)
{
    HANDLE fd;
    int rc;

    cmdname = argv[0];
    state.imgchunk = 512;
    state.speed = 115200;

    parse_opts(argc, argv);
    validate_opts();

    fd = port_open(state.devname);
    if (fd < 0) {
        exit(1);
    }
    state.port = fd;

    rc = port_setup(state.port, state.speed);
    if (rc < 0) {
        exit(1);
    }

    rc = file_read(state.filename, &state.file_sz, &state.file);
    if (rc < 0) {
        exit(1);
    }

    flush_dev_console();

    rc = echo_ctl(0);
    if (rc == 0) {
        rc = img_upload();
    }
    if (rc == 0) {
        rc = reset_device();
    }
#if 0
    if (echo_ctl(1)) {
        return 1;
    }
#endif
    free(state.file);
    fflush(stderr);
    fflush(stdout);
    return 0;
}
