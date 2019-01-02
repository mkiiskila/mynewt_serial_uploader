all: serial_upload

SRCS = \
	serial_upload.c \
	serial_upload_msg.c \
	tinycbor/src/cborparser.c \
	tinycbor/src/cborencoder.c \
	tinycbor/src/cborparser_dup_string.c \
	crc/crc16.c \
	base64/base64.c

serial_upload: $(SRCS)
	cc -o serial_upload -ggdb -Wall -I tinycbor/src -I . $^

clean:
	rm serial_upload
