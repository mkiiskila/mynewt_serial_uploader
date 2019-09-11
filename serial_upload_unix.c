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
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <errno.h>

#include "serial_upload.h"

#if __linux__
#include <libgen.h>

static char *devname;

static void
port_open_storename(const char *name)
{
    devname = strdup(name);
    if (!devname) {
        return;
    }
    devname = basename(devname);
}

static void
port_setup_lowlatency(char *string)
{
    int fd;
    char filename[128];

    snprintf(filename, sizeof(filename) - 1,
             "/sys/bus/usb-serial/devices/%s/latency_timer", devname);
    fd = open(filename, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "Warning: failed to set %s to %s: %s\n",
                filename, string, strerror(errno));
        return;
    }
    write(fd, string, strlen(string));
    close(fd);
}
#endif

int
port_open(const char *name)
{
    int fd;

    fd = open(name, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "%s: port %s open failed\n", cmdname, name);
    }
#if __linux__
    port_open_storename(name);
#endif
    return fd;
}

int
port_setup(int fd, unsigned long speed)
{
    struct termios tios;
    int rc;

    rc = tcgetattr(fd, &tios);
    if (rc < 0) {
        fprintf(stderr, "%s: tcgetattr() fail:%s\n", cmdname, strerror(errno));
        return rc;
    }

    tios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL |
      IXON | IXANY | IXOFF | IUTF8);
    tios.c_oflag &= ~(OPOST | OCRNL | OFILL |
      OFDEL | NLDLY | CRDLY | TABDLY | BSDLY | VTDLY | FFDLY);
    tios.c_oflag |= ONOCR | ONLRET;
    tios.c_cflag &= ~(CSIZE | CSTOPB | CRTSCTS);
    tios.c_cflag |= CS8 | CREAD | CLOCAL;
    tios.c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE | ECHOK |
      ECHONL | ECHOCTL | ECHOPRT | ECHOKE | FLUSHO | NOFLSH |
      TOSTOP | PENDIN | IEXTEN);

    switch (speed) {
#ifdef B115200
    case 115200:
        speed = B115200;
        break;
#endif
#ifdef B230400
    case 230400:
        speed = B230400;
        break;
#endif
#ifdef B921600
    case 921600:
        speed = B921600;
        break;
#endif
#ifdef B1000000
    case 1000000:
        speed = B1000000;
        break;
#endif
    default:
        fprintf(stderr, "Invalid speed %ld for this platform\n", speed);
        return -1;
    }
    rc = cfsetspeed(&tios, (speed_t)speed);
    if (rc < 0) {
        fprintf(stderr, "%s: cfsetspeed(%lu) fail: %s\n", cmdname, speed,
                strerror(errno));
        return rc;
    }

    rc = tcsetattr(fd, TCSAFLUSH, &tios);
    if (rc < 0) {
        fprintf(stderr, "%s: tcsetattr() fail: %s\n", cmdname, strerror(errno));
        return rc;
    }
#if __linux__
    port_setup_lowlatency("1");
#endif
    return 0;
}

int
port_write_data(int fd, void *buf, size_t len)
{
    if (write(fd, buf, len) != len) {
        fprintf(stderr, "Write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int
port_read_poll(int fd, char *buf, size_t maxlen, int end_time, int verbose)
{
    int now;
    int rc = 0;

    while (!rc) {
        now = time_get();
        if (now > end_time) {
            fprintf(stderr, "Read timed out\n");
            return -14;
        }
        rc = read(fd, buf, maxlen);
        if (rc < 0 && errno == EAGAIN) {
            rc = 0;
        }
        if (rc > 0 && verbose > 1) {
            dump_hex("RX", buf, rc);
        }
    }
    if (rc < 0) {
        fprintf(stderr, "Read failed: %d %s\n", errno, strerror(errno));
    }
    return rc;
}

int
file_read(const char *name, size_t *sz, uint8_t **bufp)
{
    FILE *fp;
    struct stat st;
    uint8_t *buf;
    int rc;

    fp = fopen(name, "r");
    if (!fp) {
        fprintf(stderr, "%s: open %s failed: %s\n", cmdname, name,
          strerror(errno));
        return -1;
    }

    rc = stat(name, &st);
    if (rc < 0) {
        fprintf(stderr, "%s: stat %s failed: %s\n", cmdname, name,
          strerror(errno));
        return -1;
    }
    *sz = st.st_size;

    buf = malloc(st.st_size);
    if (!buf) {
        fprintf(stderr, "%s: malloc() failed\n", cmdname);
        return -1;
    }
    rc = fread(buf, st.st_size, 1, fp);
    if (rc != 1) {
        fprintf(stderr, "%s: fread %s fail\n", cmdname, name);
        return -1;
    }
    *bufp = buf;
    return 0;
}

int
time_get(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec;
}
