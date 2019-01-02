#ifndef _SERIAL_UPLOAD_H_
#define _SERIAL_UPLOAD_H_

size_t serial_uploader_echo_ctl(uint8_t *buf, size_t sz, int val);
size_t serial_uploader_create_seg0(uint8_t *buf, size_t sz,
    size_t file_sz, uint8_t *data, int seglen);
size_t serial_uploader_create_segX(uint8_t *buf, size_t sz,
    size_t off, uint8_t *data, int seglen);
int serial_uploader_decode_rsp(uint8_t *buf, size_t sz, size_t *off);
int serial_uploader_is_rsp(uint8_t *buf, size_t sz);

#endif
