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
#include <windows.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sysinfoapi.h>

#include "serial_upload.h"

#ifdef __MINGW32__
#define GetTickCount64 GetTickCount
#endif

/*
 * https://docs.microsoft.com/en-us/previous-versions/ff802693(v=msdn.10)#overview
 * https://docs.microsoft.com/en-us/windows/desktop/devio/configuring-a-communications-resource
 */
HANDLE
port_open(const char *name)
{
    HANDLE fd;
    char namebuf[128];

    if (strchr(name, '\\')) {
        strcpy_s(namebuf, sizeof(namebuf), name);
    } else {
        sprintf_s(namebuf, sizeof(namebuf), "\\\\.\\\\%s", name);
    }

    fd = CreateFileA(namebuf, GENERIC_READ | GENERIC_WRITE, 0, NULL,
      OPEN_EXISTING, 0 /*FILE_FLAG_OVERLAPPED*/, NULL);
    if (fd == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "%s: CreateFileA(%s): failed - error %ld\n",
                cmdname, namebuf, GetLastError());
        return fd;
    }
    return fd;
}

int
port_setup(HANDLE fd, unsigned long speed)
{
    DCB dcb;
    COMMTIMEOUTS timeouts;

    memset(&dcb, 0, sizeof(dcb));

    if (!GetCommState(fd, &dcb)) {
        fprintf(stderr, "%s: GetCommState() failed - error %ld\n",
                cmdname, GetLastError());
        return -1;
    }

    dcb.BaudRate = speed;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;

    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;

    /*
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fAbortOnError = FALSE;
    */

    if (!SetCommState(fd, &dcb)) {
        fprintf(stderr, "%s: SetCommState() failed - error %ld\n",
                cmdname, GetLastError());
        return -1;
    }

    /*
     * Set timeouts such that reads return after 1 ms.
     */
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    if (!SetCommTimeouts(fd, &timeouts)) {
        fprintf(stderr, "%s: SetCommTimeout() failed - error %ld\n",
                cmdname, GetLastError());
        return -1;
    }

    return 0;
}

int
port_write_data(HANDLE fd, void *bufv, size_t len)
{
    uint8_t *buf = (uint8_t *)bufv;
    OVERLAPPED osWrite;
    DWORD dwWritten;
    size_t off;

    /*
     * Create this write operation's OVERLAPPED structure's hEvent.
     */
    memset(&osWrite, 0, sizeof(osWrite));
    osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (osWrite.hEvent == NULL) {
        fprintf(stderr, "%s: CreateEvent() failed\n", cmdname);
        return -1;
    }

    for (off = 0; off < len; off += dwWritten) {
        if (!WriteFile(fd, buf + off, len - off, &dwWritten, &osWrite)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                // WriteFile failed, but isn't delayed. Report error and abort.
                fprintf(stderr, "%s: WriteFile() to serial failed\n", cmdname);
                CloseHandle(osWrite.hEvent);
                return -1;
            }
        }
        if (!GetOverlappedResult(fd, &osWrite, &dwWritten, TRUE)) {
            fprintf(stderr, "%s: GetOverlappedResult() failed\n", cmdname);
            CloseHandle(osWrite.hEvent);
            return -1;
        }
    }
    CloseHandle(osWrite.hEvent);
    return 0;
}

int
port_read_poll(HANDLE fd, char *buf, size_t maxlen, int end_time, int verbose)
{
    int now;
    int rc = 0;
    DWORD len;

    while (!rc) {
        now = time_get();
        if (now > end_time) {
            fprintf(stderr, "Read timed out\n");
            return -14;
        }
        if (!ReadFile(fd, buf, maxlen, &len, NULL)) {
            fprintf(stderr, "%s: ReadFile() failed - error %d\n",
                    cmdname, GetLastError());
            return -15;
        }
        rc = len;
        if (rc > 0 && verbose > 1) {
            dump_hex("RX", buf, rc);
        }
    }
    if (rc < 0) {
        fprintf(stderr, "Read failed: %d\n", GetLastError());
    }
    return rc;
}

int
file_read(const char *name, size_t *sz, uint8_t **bufp)
{
    HANDLE fd;
    int len;
    DWORD len2;
    uint8_t *buf = NULL;

    fd = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fd == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "%s: CreateFileA(%s) failed - error %ld\n",
                cmdname, name, GetLastError());
        return -1;
    }

    len = GetFileSize(fd, NULL);
    if (len == INVALID_FILE_SIZE) {
        fprintf(stderr, "%s: GetFileSize(%s) failed - error %ld\n",
                cmdname, name, GetLastError());
        goto err;
    }

    buf = malloc(len);
    if (!buf) {
        fprintf(stderr, "%s: malloc() failure\n", cmdname);
        goto err;
    }

    if (!ReadFile(fd, buf, len, &len2, NULL)) {
        fprintf(stderr, "%s: file read len %d failed err %d",
                cmdname, len, GetLastError());
        goto err;
    }
    CloseHandle(fd);

    *bufp = buf;
    *sz = len;
    return 0;
err:
    free(buf);
    CloseHandle(fd);
    return -1;
}

int
time_get(void)
{
    return (int)(GetTickCount64() / 1000);
}
