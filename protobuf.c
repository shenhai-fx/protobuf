/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_protobuf.h"

#include "writer.h"

#define PB_COMPILE_ERROR(message, ...) PB_COMPILE_ERROR_EX(getThis(), message, __VA_ARGS__)
#define PB_COMPILE_ERROR_EX(this, message, ...) \
	zend_throw_exception_ex(NULL, 0 TSRMLS_CC, "%s: compile error - " #message, ZSTR_VAL(Z_OBJCE_P(this)->name), __VA_ARGS__)
#define PB_CONSTANT(name) \
	zend_declare_class_constant_long(pb_entry, #name, sizeof(#name) - 1, name TSRMLS_CC)
#define PB_FOREACH(iter, hash) \
	for (zend_hash_internal_pointer_reset_ex((hash), (iter)); zend_hash_has_more_elements_ex((hash), (iter)) == SUCCESS; zend_hash_move_forward_ex((hash), (iter)))

#define PB_FIELDS_METHOD "fields"
#define PB_SERIALIZE_TO_STRING_METHOD "serializeToString"

#define PB_VALUES_PROPERTY "values" 
#define PB_FIELD_TYPE_PROPERTY "type"
#define PB_FIELD_NAME_PROPERTY "name"


#define PB_METHOD(func) PHP_METHOD(ProtobufMessage, func)
#define RETURN_THIS() RETURN_ZVAL(getThis(), 1, 0);
#define IS_32_BIT (sizeof(zend_long) < sizeof(int64_t))

#define ZVAL_INT64(zval, value) \
		if ((value) > ZEND_LONG_MAX || (value) < ZEND_LONG_MIN) { \
			ZVAL_DOUBLE(zval, (double)(value)); \
		} else { \
			ZVAL_LONG(zval, (zend_long)(value)); \
		}

#define ZVAL_LVAL_INT64(zval, int64_out_p) \
		if (Z_TYPE_P(zval) ==IS_DOUBLE) {	\
			*int64_out_p = (int64_t)Z_DVAL_P(zval);	\
		} else {	\
			*int64_out_p = (int64_t)Z_LVAL_P(zval);	\
		}

enum
{
	PB_TYPE_DOUBLE = 1,
	PB_TYPE_FIXED32,
	PB_TYPE_FIXED64,
	PB_TYPE_FLOAT,
	PB_TYPE_INT,
	PB_TYPE_SIGNED_INT,
	PB_TYPE_STRING,
	PB_TYPE_BOOL
};

static int le_protobuf;
zend_class_entry *pb_entry;

static zval *pb_get_field_descriptors(zval *this);
static zval *pb_get_field_descriptor(zval *this, zval *descriptors, zend_ulong field_number);
static zval *pb_get_field_type(zval *this, zval *descriptor, zend_ulong field_number);
static const char *pb_get_field_name(zval *this, zend_ulong field_number);
static int pb_assign_value(zval *this, zval *dest, zval *src, zend_ulong field_number);
static zval *pb_get_values(zval *this);
static zval *pb_get_value(zval *this, zval *values, zend_ulong field_number);

PB_METHOD(__construct)
{
	zval values;
	array_init(&values);

	add_property_zval(getThis(), PB_VALUES_PROPERTY, &values);
}

PB_METHOD(append)
{
	zend_long field_number = -1;
	zval *old_value_array, *values, *value, val;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lz", &field_number, &value) == FAILURE) {
		RETURN_THIS();
	}

	if ((values = pb_get_values(getThis())) == NULL) {
		RETURN_THIS();
	}

	if ((old_value_array = pb_get_value(getThis(), values, (zend_ulong)field_number)) == NULL) {
		RETURN_THIS();
	}

	if (pb_assign_value(getThis(), &val, value, (zend_ulong)field_number) != 0) {
		RETURN_THIS();
	}

	add_next_index_zval(old_value_array, &val);

	RETURN_THIS();
}

PB_METHOD(clear)
{
	zend_long field_number = -1;

	zval *value, *values;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &field_number) == FAILURE) {
		RETURN_THIS();
	}

	if ((values = pb_get_values(getThis())) == NULL) {
		RETURN_THIS();
	}

	if ((value = pb_get_value(getThis(), values, (zend_ulong)field_number)) == NULL) {
		RETURN_THIS();
	}

	if (Z_TYPE_P(value) != IS_ARRAY) {
		PB_COMPILE_ERROR("'%s' field internal type should be an array", pb_get_field_name(getThis(), (zend_ulong)field_number));

		RETURN_THIS();
	}

	zend_hash_clean(Z_ARRVAL_P(value));

	RETURN_THIS();
}

PB_METHOD(set)
{
	zend_long field_number = -1;
	zval *old_value, *value, *values;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lz", &field_number, &value) == FAILURE) {
		RETURN_THIS();
	}

	if ((values = pb_get_values(getThis())) == NULL) {
		RETURN_THIS();
	}

	if ((old_value = pb_get_value(getThis(), values, (zend_ulong)field_number)) == NULL) {
		RETURN_THIS();
	}

	if (Z_TYPE_P(value) != IS_NULL) {
		zval_ptr_dtor(old_value);
		pb_assign_value(getThis(), old_value, value, field_number);

		RETURN_THIS();
	}

	if (Z_TYPE_P(old_value) != IS_NULL) {
		zval_ptr_dtor(old_value);
		ZVAL_NULL(old_value);
	}

	RETURN_THIS();
}

PB_METHOD(serializeToString)
{
	writer_t writer;

	char *pack;
	int pack_size;

	zend_ulong field_number;
	HashPosition i, j;
	zval *val, *values, *descriptors, *descriptor, *required, *type;

	if ((values = pb_get_values(getThis())) == NULL) {
		return;
	}

	if ((descriptors = pb_get_field_descriptors(this)) == NULL) {
		return;
	}

	writer_init(&writer);

	PB_FOREACH(&i, Z_ARRVAL_P(descriptors)) {
		zend_hash_get_current_key_ex(Z_ARRVAL_P(descriptors), NULL, &field_number, &i);
		descriptor = zend_hash_get_current_data_ex(Z_ARRVAL_P(descriptors), &i);

		if (ZEND_TYPE_P(descriptor) != IS_ARRAY) {
			zend_throw_exception_ex(NULL, 0 TSRMLS_CC, "%s: '%s' field descriptor must be an array", ZSTR_VAL(Z_OBJCE_P(getThis())->name), pb_get_field_name(getThis(), field_number));
			goto fail;
		}

		if ((value = zend_hash_index_find(Z_ARRVAL_P(values), field_number)) == NULL) {
			PB_COMPILE_ERROR("missing '%s' field value", pb_get_field_name(getThis(), field_number));
			goto fail;
		}

		if ((type = pb_get_field_type(getThis(), descriptor, field_number)) == NULL) {
			goto fail1;
		}

		if (Z_TYPE_P(value) == IS_NULL) {
			if ((required = zend_hash_str_find(Z_ARRVAL_P(field_descriptor), PB_FIELD_REQUIRED, sizeof(PB_FIELD_REQUIRED) - 1)) == NULL) {
				zend_throw_exception_ex(NULL, 0 TSRMLS_CC, "%s: '%s' field is required and must be set", ZSTR_VAL(Z_OBJCE_P(getThis())->name), pb_get_field_name(getThis(), field_number));
			}

			if (Z_TYPE_P(required) == IS_TRUE) {
				zend_throw_exception_ex(NULL, 0 TSRMLS_CC, "%s: '%s' field is required and must be set", ZSTR_VAL(Z_OBJCE_P(getThis())->name), pb_get_field_name(getThis(), field_number));
				goto fail;
			}

			continue;
		}

		if (Z_TYPE_P(value) == IS_ARRAY) {

			PB_FOREACH(&j, Z_ARRVAL_P(value)) {
				val = zend_hash_get_current_data_ex(Z_ARRVAL_P(value), &j);
				if (pb_serialize_field_value(getThis(), &writer, field_number, type, val) != 0) {
					goto fail;
				}
		} else if (pb_serialize_field_value(getThis(), &writer, field_number, type, val) != 0) {
			goto fail;
		}
	}

	pack = writer_get_pack(&writer, &pack_size);
	RETVAL_STRINGL(pack, pack_size);

	zval_ptr_dtor(descriptors);
	writer_free_pack(&writer);

	return;

fail:
	zval_ptr_dtor(descriptors);
	writer_free_pack(&writer);

	return;

}

PB_METHOD(get)
{
	zend_long field_number = -1;
	zval *values, *value;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &field_number) == FAILURE) {
		RETURN_THIS();
	}

	if ((values = pb_get_values(getThis())) == NULL) {
		RETURN_THIS();
	}

	if ((value = pb_get_value(getThis(), values, (zend_ulong)field_number)) == NULL) {
		RETURN_THIS();
	}

	RETURN_ZVAL(value, 1, 0);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_append, 0, 0, 2)
	ZEND_ARG_INFO(0, field_number)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_clear, 0, 0, 1)
	ZEND_ARG_INFO(0, field_number)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_set, 0, 0, 2)
	ZEND_ARG_INFO(0, field_number)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_get, 0, 0, 1)
	ZEND_ARG_INFO(0, field_number)
ZEND_END_ARG_INFO()

const zend_function_entry pb_methods[] = {
	PHP_ME(ProtobufMessage,	__construct, arginfo_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	PHP_ME(ProtobufMessage,	append, arginfo_append, ZEND_ACC_PUBLIC)
	PHP_ME(ProtobufMessage,	clear, arginfo_clear, ZEND_ACC_PUBLIC)
	PHP_ME(ProtobufMessage,	set, arginfo_set, ZEND_ACC_PUBLIC)
	PHP_ME(ProtobufMessage,	get, arginfo_get, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}	
};

PHP_MINIT_FUNCTION(protobuf)
{
	zend_class_entry zce;
	INIT_CLASS_ENTRY(zce, "ProtobufMessage", pb_methods);

	pb_entry = zend_register_internal_class(&zce);

	PB_CONSTANT(PB_TYPE_DOUBLE);
	PB_CONSTANT(PB_TYPE_FIXED32);
	PB_CONSTANT(PB_TYPE_FIXED64);
	PB_CONSTANT(PB_TYPE_FLOAT);
	PB_CONSTANT(PB_TYPE_INT);
	PB_CONSTANT(PB_TYPE_SIGNED_INT);
	PB_CONSTANT(PB_TYPE_STRING);
	PB_CONSTANT(PB_TYPE_BOOL);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(protobuf)
{
	return SUCCESS;
}

PHP_RINIT_FUNCTION(protobuf)
{
#if defined(COMPILE_DL_PROTOBUF) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(protobuf)
{
	return SUCCESS;
}

PHP_MINFO_FUNCTION(protobuf)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "protobuf support", "enabled");
	php_info_print_table_end();
}

zend_module_entry protobuf_module_entry = {
	STANDARD_MODULE_HEADER,
	"protobuf",
	pb_methods,
	PHP_MINIT(protobuf),
	PHP_MSHUTDOWN(protobuf),
	PHP_RINIT(protobuf),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(protobuf),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(protobuf),
	PHP_PROTOBUF_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_PROTOBUF
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(protobuf)
#endif

static zval *pb_get_field_descriptors(zval *this)
{
	zval *descriptors, method;
    TSRMLS_FETCH();

	ZVAL_STRINGL(&method, PB_FIELDS_METHOD, sizeof(PB_FIELDS_METHOD) - 1);

	call_user_function_ex(NULL, this, &method, descriptors, 0, NULL, 0, NULL TSRMLS_CC);

	zval_ptr_dtor(&method);

	return descriptors;
}

static zval *pb_get_field_descriptor(zval *this, zval *descriptors, zend_ulong field_number)
{
	zval *descriptor;

	TSRMLS_FETCH();

	if ((descriptor = zend_hash_index_find(Z_ARRVAL_P(descriptors), field_number)) == NULL) {
		PB_COMPILE_ERROR_EX(this, "missing %lu field descriptor", (uint64_t)field_number);
	}

	return descriptor;
}

static const char *pb_get_field_name(zval *this, zend_ulong field_number)
{
	zval *descriptors, *descriptor, *name;

	TSRMLS_FETCH();

	if ((descriptors = pb_get_field_descriptors(this)) == NULL) {
		return NULL;
	}

	if ((descriptor = pb_get_field_descriptor(this, descriptors, field_number)) == NULL) {
		goto fail0;
	}

	if ((name = zend_hash_str_find(Z_ARRVAL_P(descriptor), PB_FIELD_NAME_PROPERTY, sizeof(PB_FIELD_NAME_PROPERTY) -1)) == NULL) {
		PB_COMPILE_ERROR_EX(this, "missing %lu field name property in field descriptor", (uint64_t)field_number);
		goto fail0;
	}

	zval_ptr_dtor(descriptors);
	return (const char *)Z_STRVAL_P(name);

fail0:
	zval_ptr_dtor(descriptors);
	return NULL;
}

static zval *pb_get_field_type(zval *this, zval *descriptor, zend_ulong field_number)
{
	zval *type;

	TSRMLS_FETCH();

	if ((type = zend_hash_str_find(Z_ARRVAL_P(descriptor), PB_FIELD_TYPE_PROPERTY, sizeof(PB_FIELD_TYPE_PROPERTY) -1)) == NULL) {
		PB_COMPILE_ERROR_EX(this, "missing '%lu' field type property in field descriptor", pb_get_field_name(this, field_number));
	}

	return type;
}

static int pb_assign_value(zval *this, zval *dest, zval *src, zend_ulong field_number)
{
	zval *descriptors, *descriptor, *type, tmp;

	TSRMLS_FETCH();

	if ((descriptors = pb_get_field_descriptors(this)) == NULL) {
		goto fail0;
	}

	if ((descriptor = pb_get_field_descriptor(this, descriptors, field_number)) == NULL) {
		goto fail1;
	}

	if ((type = pb_get_field_type(this, descriptor, field_number)) == NULL) {
		goto fail1;
	}

	ZVAL_DUP(&tmp, src);

	if (Z_TYPE_P(type) == IS_LONG) {
		switch (Z_LVAL_P(type)) {
			case PB_TYPE_DOUBLE:
			case PB_TYPE_FLOAT:
				if (Z_TYPE(tmp) != IS_DOUBLE) {
					convert_to_explicit_type(&tmp, IS_DOUBLE);
				}
				break;

			case PB_TYPE_FIXED32:
			case PB_TYPE_BOOL:
				if (Z_TYPE(tmp) != IS_LONG) {
					convert_to_explicit_type(&tmp, IS_LONG);
				}
				break;

			case PB_TYPE_INT:
			case PB_TYPE_FIXED64:
			case PB_TYPE_SIGNED_INT:
				if ((Z_TYPE(tmp) == IS_DOUBLE) && IS_32_BIT) {
					if ((double)ZEND_LONG_MAX < Z_DVAL(tmp)) {
						// store big value for 32bit systems as double
						break;
					}
				}

				convert_to_explicit_type(&tmp, IS_LONG);
				break;

			case PB_TYPE_STRING:
				if (Z_TYPE(tmp) != IS_STRING) {
					convert_to_explicit_type(&tmp, IS_STRING);
				}
				break;

			default:
				PB_COMPILE_ERROR_EX(this, "unexpected '%s' field type %d in field descriptor", pb_get_field_name(this, field_number), zend_get_type_by_const(Z_LVAL_P(type)));
				goto fail2;
		}
	} else if (Z_TYPE_P(type) != IS_STRING) {
		PB_COMPILE_ERROR_EX(this, "unexpected %s type of '%s' field type in field descriptor", zend_get_type_by_const(Z_TYPE_P(type)), pb_get_field_name(this, field_number));
		goto fail2;
	}

	*dest = tmp;
	zval_ptr_dtor(descriptors);

	return 0;

fail2:
	zval_ptr_dtor(&tmp);
fail1:
	zval_ptr_dtor(descriptors);
fail0:
	return -1;
}

static zval *pb_get_values(zval *this)
{
    TSRMLS_FETCH();

    return zend_hash_str_find(Z_OBJPROP_P(this), PB_VALUES_PROPERTY, sizeof(PB_VALUES_PROPERTY) - 1);
}

static zval *pb_get_value(zval *this, zval *values, zend_ulong field_number) 
{
	zval *value;

	TSRMLS_FETCH();

	if ((value = zend_hash_index_find(Z_ARRVAL_P(values), field_number)) == NULL ) {
		PB_COMPILE_ERROR_EX(this, "missing %lu field value", (uint64_t)field_number);
	}

	return value;
}

static int pb_serialize_field_value(zval *this, writer_t *writer, zend_ulong field_number, zval *type, zval *value)
{
	int r;
	zval ret, method;
	int64_t int64_value;

	TSRMLS_FETCH();

	if (Z_TYPE_P(type) == IS_STRING) {
		//处理嵌套message
		ZVAL_STRINGL(&method, PB_SERIALIZE_TO_STRING_METHOD, sizeof(PB_SERIALIZE_TO_STRING_METHOD) - 1);

		if (call_user_function(NULL, value, &method, &ret, 0, NULL TSRMLS_CC) == FAILURE) {
			zval_ptr_dtor(&method);
			return -1;
		}
		zval_ptr_dtor(&method);

		if (Z_TYPE(ret) != IS_STRING) {
			zval_ptr_dtor(&ret);
			return -1;
		}

		if (Z_STRLEN(ret)) {
			writer_write_message(writer, (uint64_t)field_number, Z_STRVAL(ret), Z_STRLEN(ret));
		}

		zval_ptr_dtor(&ret);
	} else if ((Z_TYPE_P(type) == IS_LONG) {

		switch (Z_LVAL_P(type)) {
			case PB_TYPE_DOUBLE:
				r = writer_write_double(writer, (uint64_t)field_number, Z_DVAL_P(value));
				break;

			case PB_TYPE_FIXED32:
				r = writer_write_fixed32(writer, (uint64_t)field_number, Z_LVAL_P(value));
				break;

			case PB_TYPE_BOOL:
			case PB_TYPE_INT:
				Z_LVAL_INT64(value, &int64_value);
				r = writer_write_int(writer, (uint64_t)field_number, int64_value);
				break;

			case PB_TYPE_FIXED64:
				Z_LVAL_INT64(value, &int64_value);
				r = writer_write_int(writer, (uint64_t)field_number, int64_value);
				break;

			case PB_TYPE_FLOAT:
				r = writer_write_float(writer, (uint64_t)field_number, Z_DVAL_P(value));
				break;

			case PB_TYPE_SIGNED_INT:
				Z_LVAL_INT64(value, &int64_value);
				r = writer_write_signed_int(writer, (uint64_t)field_number, int64_value);
				break;

			case PB_TYPE_STRING:
				r = writer_write_string(writer, (uint64_t)field_number, Z_STRVAL_P(value), Z_STRLEN_P(value));
				break;

			default:
				PB_COMPILE_ERROR_EX(this, "unexpected '%s' field type %d in field descriptor", pb_get_field_name(this, field_number), Z_LVAL_P(type));
				return -1;
		}

		if (r != 0) {
			return -1;
		}
	} else {
		PB_COMPILE_ERROR_EX(this, "unexpected %s type of '%s' field type in field descriptor", zend_get_type_by_const(Z_TYPE_P(type)), pb_get_field_name(this, field_number));
		return -1;
	}

	return 0;
}
