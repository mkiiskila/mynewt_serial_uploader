
#include <arpa/inet.h>

#include "cbor.h"

#include "serial_upload.h"

struct nmgr_hdr {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t  nh_op:3;           /* NMGR_OP_XXX */
    uint8_t  _res1:5;
#endif
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    uint8_t  _res1:5;
    uint8_t  nh_op:3;           /* NMGR_OP_XXX */
#endif
    uint8_t  nh_flags;          /* XXX reserved for future flags */
    uint16_t nh_len;            /* length of the payload */
    uint16_t nh_group;          /* NMGR_GROUP_XXX */
    uint8_t  nh_seq;            /* sequence number */
    uint8_t  nh_id;             /* message ID within group */
};

#define NMGR_OP_READ            (0)
#define NMGR_OP_READ_RSP        (1)
#define NMGR_OP_WRITE           (2)
#define NMGR_OP_WRITE_RSP       (3)

/* First 64 groups are reserved for system level newtmgr commands.
 * Per-user commands are then defined after group 64.
 */
#define MGMT_GROUP_ID_DEFAULT   (0)
#define MGMT_GROUP_ID_IMAGE     (1)
#define MGMT_GROUP_ID_STATS     (2)
#define MGMT_GROUP_ID_CONFIG    (3)
#define MGMT_GROUP_ID_LOGS      (4)
#define MGMT_GROUP_ID_CRASH     (5)
#define MGMT_GROUP_ID_SPLIT     (6)
#define MGMT_GROUP_ID_RUN       (7)
#define MGMT_GROUP_ID_FS        (8)
#define MGMT_GROUP_ID_PERUSER   (64)

#define NMGR_ID_ECHO            0
#define NMGR_ID_CONS_ECHO_CTRL  1
#define NMGR_ID_TASKSTATS       2
#define NMGR_ID_MPSTATS         3
#define NMGR_ID_DATETIME_STR    4
#define NMGR_ID_RESET           5

#define IMGMGR_NMGR_ID_STATE        0
#define IMGMGR_NMGR_ID_UPLOAD       1
#define IMGMGR_NMGR_ID_FILE         2
#define IMGMGR_NMGR_ID_CORELIST     3
#define IMGMGR_NMGR_ID_CORELOAD     4
#define IMGMGR_NMGR_ID_ERASE        5
#define IMGMGR_NMGR_ID_ERASE_STATE  6

size_t
serial_uploader_echo_ctl(uint8_t *buf, size_t sz, int val)
{
	int rc;
	CborEncoder enc;
	CborEncoder map;
	struct nmgr_hdr *nh;
	int len;

	nh = (struct nmgr_hdr *)buf;
	memset(nh, 0, sizeof(*nh));
	nh->nh_op = NMGR_OP_WRITE;
	nh->nh_group = htons(MGMT_GROUP_ID_DEFAULT);
	nh->nh_id = NMGR_ID_CONS_ECHO_CTRL;

	cbor_encoder_init(&enc, (void *)(nh + 1), sz, 0);

	rc = cbor_encoder_create_map(&enc, &map, CborIndefiniteLength);

	rc |= cbor_encode_text_stringz(&map, "_h");
	rc |= cbor_encode_byte_string(&map, (void *)&nh, sizeof(nh));

	rc |= cbor_encode_text_stringz(&map, "echo");
	rc |= cbor_encode_int(&map, val);

	rc |= cbor_encoder_close_container(&enc, &map);
	if (rc) {
		return -1;
	}
	len = cbor_encoder_get_buffer_size(&enc, (void *)(nh + 1));
	nh->nh_len = htons(len);

	return len + sizeof(*nh);
}

size_t
serial_uploader_create_seg0(uint8_t *buf, size_t sz,
    size_t file_sz, uint8_t *data, int seglen)
{
	int rc;
	int len;
	CborEncoder enc;
	CborEncoder map;
	struct nmgr_hdr *nh;

	nh = (struct nmgr_hdr *)buf;
	memset(nh, 0, sizeof(*nh));
	nh->nh_op = NMGR_OP_WRITE;
	nh->nh_group = htons(MGMT_GROUP_ID_IMAGE);
	nh->nh_id = IMGMGR_NMGR_ID_UPLOAD;

	cbor_encoder_init(&enc, (void *)(nh + 1), sz, 0);

	rc = cbor_encoder_create_map(&enc, &map, CborIndefiniteLength);

	rc |= cbor_encode_text_stringz(&map, "_h");
	rc |= cbor_encode_byte_string(&map, (void *)&nh, sizeof(nh));

	rc |= cbor_encode_text_stringz(&map, "sha");
	rc |= cbor_encode_byte_string(&map, NULL, 0);
	rc |= cbor_encode_text_stringz(&map, "off");
	rc |= cbor_encode_uint(&map, 0);
	rc |= cbor_encode_text_stringz(&map, "len");
	rc |= cbor_encode_uint(&map, file_sz);
	rc |= cbor_encode_text_stringz(&map, "data");
	rc |= cbor_encode_byte_string(&map, data, seglen);
	rc |= cbor_encoder_close_container(&enc, &map);
	if (rc) {
		return -1;
	}
	len = cbor_encoder_get_buffer_size(&enc, (void *)(nh + 1));
	nh->nh_len = htons(len);

	return len + sizeof(*nh);
}

size_t
serial_uploader_create_segX(uint8_t *buf, size_t sz,
    size_t off, uint8_t *data, int seglen)
{
	int rc;
	int len;
	CborEncoder enc;
	CborEncoder map;
	struct nmgr_hdr *nh;

	nh = (struct nmgr_hdr *)buf;
	memset(nh, 0, sizeof(*nh));
	nh->nh_op = NMGR_OP_WRITE;
	nh->nh_group = htons(MGMT_GROUP_ID_IMAGE);
	nh->nh_id = IMGMGR_NMGR_ID_UPLOAD;

	cbor_encoder_init(&enc, (void *)(nh + 1), sz, 0);

	rc = cbor_encoder_create_map(&enc, &map,  CborIndefiniteLength);

	rc |= cbor_encode_text_stringz(&map, "_h");
	rc |= cbor_encode_byte_string(&map, (void *)&nh, sizeof(nh));

	rc |= cbor_encode_text_stringz(&map, "off");
	rc |= cbor_encode_uint(&map, off);
	rc |= cbor_encode_text_stringz(&map, "data");
	rc |= cbor_encode_byte_string(&map, data, seglen);

	rc |= cbor_encoder_close_container(&enc, &map);
	if (rc) {
		return -1;
	}
	len = cbor_encoder_get_buffer_size(&enc, (void *)(nh + 1));
	nh->nh_len = htons(len);

	return len + sizeof(*nh);
}

int
serial_uploader_is_rsp(uint8_t *buf, size_t sz)
{
	struct nmgr_hdr *nh;

	if (sz < sizeof(*nh)) {
		return 0;
	}

	nh = (struct nmgr_hdr *)buf;
	if (nh->nh_op == NMGR_OP_READ_RSP || nh->nh_op == NMGR_OP_WRITE_RSP) {
		return 1;
	}
	return 0;
}

int
serial_uploader_decode_rsp(uint8_t *buf, size_t sz, size_t *off)
{
	CborParser parser;
	CborValue map_val;
	CborValue val;
	int rc;
	int64_t val64;
	int64_t rsp_rc;
	int64_t rsp_off;
	char *name;
	size_t nlen;

	buf += sizeof(struct nmgr_hdr);
	sz -= sizeof(struct nmgr_hdr);
	rc = cbor_parser_init(buf, sz, 0, &parser, &map_val);
	if (rc) {
		return rc;
	}

	if (cbor_value_get_type(&map_val) != CborMapType) {
		return -2;
	}
	if (cbor_value_enter_container(&map_val, &val)) {
		return -3;
	}
	while (!cbor_value_at_end(&val)) {
		if (cbor_value_get_type(&val) != CborTextStringType) {
			break;
		}
		nlen = 0;
		rc = cbor_value_dup_text_string(&val, &name, &nlen, &val);
		if (rc) {
			return -5;
		}
		if (cbor_value_get_type(&val) != CborIntegerType) {
			return -6;
		}
		cbor_value_get_int64(&val, &val64);
		if (!strcmp(name, "rc")) {
			rsp_rc = val64;
		} else if (!strcmp(name, "off")) {
			rsp_off = val64;
		}
		cbor_value_advance(&val);
	}
	*off = rsp_off;

	return rsp_rc;
}
