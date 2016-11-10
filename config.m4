dnl $Id$
dnl config.m4 for extension protobuf

PHP_ARG_ENABLE(protobuf, whether to enable protobuf support,
[  --enable-protobuf           Enable protobuf support])

if test "$PHP_PROTOBUF" != "no"; then
  PHP_NEW_EXTENSION(protobuf, protobuf.c, writer.c $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi

PHP_C_BIGENDIAN()
