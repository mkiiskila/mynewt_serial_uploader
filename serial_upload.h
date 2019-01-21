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

#ifndef _SERIAL_UPLOAD_H_
#define _SERIAL_UPLOAD_H_

#ifndef WIN32
typedef int HANDLE;
#endif

size_t serial_uploader_echo_ctl(uint8_t *buf, size_t sz, int val);
size_t serial_uploader_reset(uint8_t *buf, size_t sz, int val);
size_t serial_uploader_create_seg0(uint8_t *buf, size_t sz,
    size_t file_sz, uint8_t *data, int seglen);
size_t serial_uploader_create_segX(uint8_t *buf, size_t sz,
    size_t off, uint8_t *data, int seglen);
int serial_uploader_decode_rsp(uint8_t *buf, size_t sz, size_t *off);
int serial_uploader_is_rsp(uint8_t *buf, size_t sz);

HANDLE port_open(const char *name);
int port_setup(HANDLE fd, unsigned long speed);
int port_write_data(HANDLE fd, void *buf, size_t len);
int port_read_poll(HANDLE fd, char *buf, size_t maxlen, int end_time,
                   int verbose);
int file_read(const char *name, size_t *sz, uint8_t **bufp);
int time_get(void);

void dump_hex(const char *hdr, void *bufv, int cnt);

extern const char *cmdname;

#endif
