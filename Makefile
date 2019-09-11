all: serial_upload

SRCS = \
	serial_upload.c \
	serial_upload_unix.c \
	serial_upload_msg.c \
	tinycbor/src/cborparser.c \
	tinycbor/src/cborencoder.c \
	tinycbor/src/cborparser_dup_string.c \
	crc/crc16.c \
	base64/base64.c

WINSRCS = \
	serial_upload.c \
	serial_upload_win.c \
	serial_upload_msg.c \
	tinycbor/src/cborparser.c \
	tinycbor/src/cborencoder.c \
	tinycbor/src/cborparser_dup_string.c \
	crc/crc16.c \
	base64/base64.c

.PHONY: all

win64: tinycbor $(SRCS) serial_upload.h
	x86_64-w64-mingw32-gcc $(WINSRCS) -lws2_32 -I ./tinycbor/src -I . -o serial_upload.exe

all: tinycbor serial_upload

tinycbor/src/%.c:
	git clone https://github.com/01org/tinycbor.git
	cd tinycbor && git checkout 04ada5890cc74e22fe31123b7f4e648b2fc1d259

serial_upload: $(SRCS) serial_upload.h
	@echo serial_upload
	$(CC) -o serial_upload -ggdb -Wall -I tinycbor/src -I . $(SRCS)

clean:
	rm serial_upload
