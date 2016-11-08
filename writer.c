#include <string.h>

#include <php.h>

#include "writer.h"

#define WRITER_DATA_SIZE_INCREMENT 1024
#define WRITER_VARINT_SPACE 10
#define WRITER_32BIT_SPACE 4
#define WRITER_64BIT_SPACE 8

static inline int writer_ensure_space(writer_t *writer, size_t space);
static inline void writer_write_varint(writer_t *writer, int64_t value);

static inline void write_fixed32(fixed32_t fix, uint8_t *out);
static inline void write_fixed64(fixed64_t fix, uint8_t *out)

void writer_free_pack(writer_t *writer)
{
	if (writer->data != NULL) {
		efree(writer->data);
		writer->data = NULL;

		writer->pos  = 0;
		writet->size = 0;
	}
}

void writer_init(writer_t *writer)
{
	if ((writer->data = emalloc(WRITER_DATA_SIZE_INCREMENT)) != NULL) {
		writet->size = WRITER_DATA_SIZE_INCREMENT;
	} else {
		writet->size = 0;
	}

	writer->pos = 0;
}

char *writer_get_pack(writer_t *writer, int *size)
{
	*size = writer->pos;
	writer->data[writer->pos] = '\0';

	return (char *)writer->data;
}

int writer_write_double(writer_t *writer, uint64_t field_number, double value)
{
	int64_t key;
	fixed64_t val;

	if (writer_ensure_space(writer, WRITER_VARINT_SPACE + WRITER_64BIT_SPACE) != 0) {
		return -1;
	}

	key = (field_number << 3) | WIRE_TYPE_64BIT;
	writer_write_varint(writer, key);

	val.d_val = value;
	write_fixed64(val, writer->data + writer->pos);
	writer->pos += WRITER_64BIT_SPACE;

	return 0;
}

int writer_write_fixed32(writer_t *writer, uint64_t field_number, int32_t value)
{
	int64_t key;
	fixed32_t val;

	if (writer_ensure_space(writer, WRITER_VARINT_SPACE + WRITER_32BIT_SPACE) != 0)
		return -1;

	key = (field_number << 3 | WIRE_TYPE_32BIT);

	writer_write_varint(writer, key);

	val.i_val = value;
	write_fixed32(val, writer->data + writer->pos);
	writer->pos += WRITER_32BIT_SPACE;

	return 0;
}

int writer_write_fixed64(writer_t *writer, uint64_t field_number, int64_t value)
{
	int64_t key;
	fixed64_t val;

	if (writer_ensure_space(writer, WRITER_VARINT_SPACE + WRITER_64BIT_SPACE) != 0)
		return -1;

	key = (field_number << 3 | WIRE_TYPE_64BIT);

	writer_write_varint(writer, key);

	val.i_val = value;
	write_fixed64(val, writer->data + writer->pos);
	writer->pos += WRITER_64BIT_SPACE;

	return 0;
}

int writer_write_float(writer_t *writer, uint64_t field_number, double value)
{
	int64_t key;
	fixed32_t val;

	if (writer_ensure_space(writer, WRITER_32BIT_SPACE) != 0) {
		return -1;
	}

	val.f_val = value;
	writer_write_fixed32(val, writer->data + writer->pos);
	writer->pos += WRITER_32BIT_SPACE;

	return 0;
}

int writer_write_int(writer_t *writer, uint64_t field_number, int64_t value)
{
	int64_t key;

	if (writer_ensure_space(writer, 2*WRITER_VARINT_SPACE) != 0) {
		return -1;
	}

	key = (field_number << 3) | WIRE_TYPE_VARINT;

	writer_write_varint(writer, key);
	writer_write_varint(writer, value);

	return 0;
}

int writer_write_signed_int(writer_t *writer, uint64_t field_number, int64_t value)
{
	int64_t key;
	uint64_t varint_val;

	if (writer_ensure_space(writer, 2*WRITER_VARINT_SPACE) != 0) {
		return -1;
	}

	key = (field_number << 3) | WIRE_TYPE_VARINT;

	writer_write_varint(writer, key);

	if (value < 0) {
		varint_val = *(uint64_t *) &(-value << 1 - 1);
	} else {
		varint_val = *(uint64_t *) &(value << 1);
	}

	writer_write_varint(writer, varint_val);

	return 0;
}

int writer_write_string(writer_t *writer, uint64_t field_number, const char *str, size_t len)
{
	int64_t key;

	if (writer_ensure_space(writer, 2*WIRE_TYPE_LENGTH_DELIMITED + len) != 0) {
		return -1;
	}

	key = (field_number << 3 | WIRE_TYPE_LENGTH_DELIMITED);

	writer_write_varint(writer, key);
	writer_write_varint(writer, len);

	memcpy(writer->data + writer->pos, str, len);
	writer->pos += len;

	return 0;
}

static inline int writer_ensure_space(writer_t *writer, size_t space)
{
	uint8_t *dat;

	if ((writer->pos + space - 1) > writer->size) {
		writer->size += space;

		if (NULL == (dat = (uint8_t *)erealloc(writer->data, writer->size)) {
			return -1;
		}

		writer->data = dat;
	}

	return 0;
}

static inline void writer_write_varint(writer_t *writer, int64_t value)
{
	if (value == 0) {
		writer->data[writer->pos++] = 0;
	} else if (value > 0) {
		do {
			byte = value;
			value >>= 7;

			if (value != 0) {
				byte |= 0x80;
			} else {
				byte &= 0x7F;
			}

			writer->data[writer->pos++] = byte;
		} while (value != 0)
	} else {
		for (i = 0; i < WRITER_VARINT_SPACE - 1; i++) {
			writer->data[writer->pos++] = value | 0x80;
			*(uint64_t *) &value >>= 7;
		}

		writer->data[writer->pos++] = value & 0x7F;
	}
}

static inline void write_fixed32(fixed32_t fix, uint8_t *out)
{
#ifndef WORDS_BIGENDIAN
	memcpy(out, &fix.u_val, 4);
#else
	out[0] = fix.u_val;
	out[1] = fix.u_val >> 8;
	out[2] = fix.u_val >> 16;
	out[3] = fix.u_val >> 24;
#endif
}

static inline void write_fixed64(fixed64_t fix, uint8_t *out)
{
#ifndef WORDS_BIGENDIAN
	memcpy(out, &fix.u_val, 8);
#else
	fixed32_t low;
	low.u_val = fix.u_val;

	fixed32_t high;
	high.u_val = fix.u_val >> 32;

	write_fixed32(low, out);
	write_fixed32(high, out + 4);
#endif
}
