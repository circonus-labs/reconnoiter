/*
 * Copyright (c) 2005-2008, Message Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name Message Systems, Inc. nor the names
 *      of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PHP_JLOG_H
#define PHP_JLOG_H

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>

#ifdef HAVE_JLOG

#include <php_ini.h>
#include <SAPI.h>
#include <ext/standard/info.h>
#include <Zend/zend_extensions.h>
#ifdef  __cplusplus
} // extern "C" 
#endif
#include <jlog.h>
#ifdef  __cplusplus
extern "C" {
#endif

extern zend_module_entry jlog_module_entry;
#define phpext_jlog_ptr &jlog_module_entry

#ifdef PHP_WIN32
#define PHP_JLOG_API __declspec(dllexport)
#else
#define PHP_JLOG_API
#endif

PHP_MINIT_FUNCTION(jlog);
PHP_MSHUTDOWN_FUNCTION(jlog);
PHP_RINIT_FUNCTION(jlog);
PHP_RSHUTDOWN_FUNCTION(jlog);
PHP_MINFO_FUNCTION(jlog);

#ifdef ZTS
#include "TSRM.h"
#endif

#define FREE_RESOURCE(resource) zend_list_delete(Z_LVAL_P(resource))

#define PROP_GET_LONG(name)    Z_LVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_LONG(name, l) zend_update_property_long(_this_ce, _this_zval, #name, strlen(#name), l TSRMLS_CC)

#define PROP_GET_DOUBLE(name)    Z_DVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_DOUBLE(name, d) zend_update_property_double(_this_ce, _this_zval, #name, strlen(#name), d TSRMLS_CC)

#define PROP_GET_STRING(name)    Z_STRVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_GET_STRLEN(name)    Z_STRLEN_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_STRING(name, s) zend_update_property_string(_this_ce, _this_zval, #name, strlen(#name), s TSRMLS_CC)
#define PROP_SET_STRINGL(name, s, l) zend_update_property_string(_this_ce, _this_zval, #name, strlen(#name), s, l TSRMLS_CC)


PHP_METHOD(Jlog, __construct);
ZEND_BEGIN_ARG_INFO(Jlog____construct_args, 0)
  ZEND_ARG_INFO(0, options)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog, add_subscriber);
ZEND_BEGIN_ARG_INFO(Jlog__add_subscriber_args, 0)
  ZEND_ARG_INFO(0, subscriber)
  ZEND_ARG_INFO(0, whence)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog, remove_subscriber);
ZEND_BEGIN_ARG_INFO(Jlog__remove_subscriber_args, 0)
  ZEND_ARG_INFO(0, subscriber)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog, list_subscribers);
ZEND_BEGIN_ARG_INFO(Jlog__list_subscribers_args, 0)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog, alter_journal_size);
ZEND_BEGIN_ARG_INFO(Jlog__alter_journal_size_args, 0)
  ZEND_ARG_INFO(0, size)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog, raw_size);
ZEND_BEGIN_ARG_INFO(Jlog__raw_size_args, 0)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog, close);
ZEND_BEGIN_ARG_INFO(Jlog__close_args, 0)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog_Writer, open);
ZEND_BEGIN_ARG_INFO(Jlog_Writer__open_args, 0)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog_Writer, write);
ZEND_BEGIN_ARG_INFO(Jlog_Writer__write_args, 0)
  ZEND_ARG_INFO(0, buffer)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog_Reader, open);
ZEND_BEGIN_ARG_INFO(Jlog_Reader__open_args, 0)
  ZEND_ARG_INFO(0, subscriber)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog_Reader, read);
ZEND_BEGIN_ARG_INFO(Jlog_Reader__read_args, 0)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog_Reader, checkpoint);
ZEND_BEGIN_ARG_INFO(Jlog_Reader__checkpoint_args, 0)
ZEND_END_ARG_INFO()
PHP_METHOD(Jlog_Reader, auto_checkpoint);
ZEND_BEGIN_ARG_INFO(Jlog_Reader__auto_checkpoint_args, 0)
  ZEND_ARG_INFO(0, state)
ZEND_END_ARG_INFO()
#ifdef  __cplusplus
} // extern "C" 
#endif

#endif /* PHP_HAVE_JLOG */

#endif /* PHP_JLOG_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
