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

#include "php.h"
#include "php_ini.h"
#include "php_jlog.h"
#include <fcntl.h>

#if HAVE_JLOG

typedef struct {
  zend_object zo;
  jlog_ctx *ctx;
  char *path;
  jlog_id start;
  jlog_id last;
  jlog_id end;
  int auto_checkpoint;
} jlog_obj;

static zend_class_entry * Jlog_ce_ptr = NULL;
static zend_class_entry * Jlog_Writer_ce_ptr = NULL;
static zend_class_entry * Jlog_Reader_ce_ptr = NULL;
static zend_object_handlers jlog_object_handlers;


static void FREE_JLOG_OBJ(jlog_obj *intern)
{
  if(intern) {
    if(intern->ctx) {
      jlog_ctx_close(intern->ctx);
      intern->ctx = NULL;
    }
    if(intern->path) {
      free(intern->path);
    }
  }
}

static void jlog_obj_dtor(void *object TSRMLS_DC)
{
  zend_object *zo = (zend_object *) object;
  jlog_obj *intern = (jlog_obj *) zo;
  FREE_JLOG_OBJ(intern);
  zend_object_std_dtor(&intern->zo TSRMLS_CC);
  efree(intern);
}

zend_object_value jlog_objects_new(zend_class_entry *class_type TSRMLS_DC)
{
  zend_object_value retval;
  jlog_obj *intern;

  intern = emalloc(sizeof(*intern));
  memset(intern, 0, sizeof(*intern));
  
  zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
  retval.handle = zend_objects_store_put(intern, (zend_objects_store_dtor_t) zend_objects_destroy_object, jlog_obj_dtor, NULL TSRMLS_CC);
  retval.handlers = &jlog_object_handlers;
  return retval;
}

/* {{{ Class definitions */

/* {{{ Class Jlog */


/* {{{ Methods */


/* {{{ proto object Jlog __construct(string path [, array options])
 */
PHP_METHOD(Jlog, __construct)
{
  zend_class_entry * _this_ce;
  zval * _this_zval;
  const char * path = NULL;
  int path_len = 0;
  int options = 0;
  int size = 0;
  jlog_obj *jo;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|ll", &path, &path_len, &options, &size) == FAILURE) { 
    return;
  }

  _this_zval = getThis();
  _this_ce = Z_OBJCE_P(_this_zval);
  jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);

  jo->ctx = jlog_new(path);
  jo->path = strdup(path);
  if(options & O_CREAT) {
    if(size) {
      jlog_ctx_alter_journal_size(jo->ctx, size);
    }
    if(jlog_ctx_init(jo->ctx) != 0) {
      if(jlog_ctx_err(jo->ctx) == JLOG_ERR_CREATE_EXISTS) {
        if(options & O_EXCL) {
          FREE_JLOG_OBJ(jo);
          efree(jo);
          php_error(E_WARNING, "file already exists: %s", path);
        }
      } else {
        int err = jlog_ctx_err(jo->ctx);
        const char *err_string = jlog_ctx_err_string(jo->ctx);
        FREE_JLOG_OBJ(jo);
        efree(jo);
        php_error(E_WARNING, "error initializing jlog: %d %s", err, err_string);
        RETURN_FALSE;
      }
    }
    jlog_ctx_close(jo->ctx);
    jo->ctx = jlog_new(path);
    if(!jo->ctx) {
      FREE_JLOG_OBJ(jo);
      efree(jo);
      php_error(E_WARNING, "jlog_new(%s) failed after successful init", path); 
      RETURN_FALSE;
    }
  }
}
/* }}} __construct */



/* {{{ proto bool add_subscriber(string subscriber [, int whence])
   */
PHP_METHOD(Jlog, add_subscriber)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
	const char * subscriber = NULL;
	int subscriber_len = 0;
	long whence = 0;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os|l", &_this_zval, Jlog_ce_ptr, &subscriber, &subscriber_len, &whence) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx || jlog_ctx_add_subscriber(jo->ctx, subscriber, whence) != 0)
  {
    RETURN_FALSE;
  }
	RETURN_TRUE;
}
/* }}} add_subscriber */



/* {{{ proto bool remove_subscriber(string subscriber)
   */
PHP_METHOD(Jlog, remove_subscriber)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
	const char * subscriber = NULL;
	int subscriber_len = 0;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &_this_zval, Jlog_ce_ptr, &subscriber, &subscriber_len) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx || jlog_ctx_remove_subscriber(jo->ctx, subscriber) != 0)
  {
    RETURN_FALSE;
  }
	RETURN_TRUE;
}
/* }}} remove_subscriber */



/* {{{ proto array list_subscribers()
   */
PHP_METHOD(Jlog, list_subscribers)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
  jlog_obj *jo;
  char **list;
  int i;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &_this_zval, Jlog_ce_ptr) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) {
    RETURN_NULL();
  }
	array_init(return_value);
  jlog_ctx_list_subscribers(jo->ctx, &list);
  for(i=0; list[i]; i++) {
    add_index_string(return_value, i, list[i], 1);
  }
  jlog_ctx_list_subscribers_dispose(jo->ctx, list);
}
/* }}} list_subscribers */



/* {{{ proto int raw_size()
   */
PHP_METHOD(Jlog, raw_size)
{
  long size;
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &_this_zval, Jlog_ce_ptr) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) {
    php_error(E_WARNING, "no valid context"); 
	  RETURN_LONG(0);
  }
  size = jlog_raw_size(jo->ctx);
  RETURN_LONG(size);
}
/* }}} raw_size */



/* {{{ proto void close()
   */
PHP_METHOD(Jlog, close)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &_this_zval, Jlog_ce_ptr) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) { return; }
  jlog_ctx_close(jo->ctx);
  jo->ctx = NULL;
}
/* }}} close */


static zend_function_entry Jlog_methods[] = {
	PHP_ME(Jlog, __construct, Jlog____construct_args, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog, add_subscriber, Jlog__add_subscriber_args, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog, remove_subscriber, Jlog__remove_subscriber_args, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog, list_subscribers, NULL, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog, raw_size, NULL, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog, close, NULL, /**/ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

/* }}} Methods */

static void class_init_Jlog(TSRMLS_D)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Jlog", Jlog_methods);
  ce.create_object = jlog_objects_new;
	Jlog_ce_ptr = zend_register_internal_class(&ce TSRMLS_CC);
	Jlog_ce_ptr->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;
}

/* }}} Class Jlog */

/* {{{ Class Jlog_Writer */


/* {{{ Methods */


/* {{{ proto object Jlog_Writer open()
   */
PHP_METHOD(Jlog_Writer, open)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &_this_zval, Jlog_Writer_ce_ptr) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) {
    RETURN_NULL();
  }
  if(jlog_ctx_open_writer(jo->ctx) != 0) {
    php_error(E_WARNING, "jlog_ctx_open_writer failed");
    RETURN_NULL();
  }
  ZVAL_ADDREF(_this_zval);
  return_value = _this_zval;
}
/* }}} open */



/* {{{ proto bool write(string buffer)
   */
PHP_METHOD(Jlog_Writer, write)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
	const char * buffer = NULL;
	int buffer_len = 0;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &_this_zval, Jlog_Writer_ce_ptr, &buffer, &buffer_len) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) {
	  RETURN_FALSE;
  }
  if(jlog_ctx_write(jo->ctx, buffer, buffer_len) < 0) {
    RETURN_FALSE;
  }
  RETURN_TRUE;
}
/* }}} write */


static zend_function_entry Jlog_Writer_methods[] = {
	PHP_ME(Jlog_Writer, open, NULL, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog_Writer, write, Jlog_Writer__write_args, /**/ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

/* }}} Methods */

static void class_init_Jlog_Writer(TSRMLS_D)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Jlog_Writer", Jlog_Writer_methods);
  ce.create_object = jlog_objects_new;
	Jlog_Reader_ce_ptr = zend_register_internal_class_ex(&ce, Jlog_ce_ptr, NULL TSRMLS_CC);
}

/* }}} Class Jlog_Writer */

/* {{{ Class Jlog_Reader */

/* {{{ Methods */


/* {{{ proto object Jlog_Reader open(string subscriber)
   */
PHP_METHOD(Jlog_Reader, open)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
	const char * subscriber = NULL;
	int subscriber_len = 0;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os", &_this_zval, Jlog_Reader_ce_ptr, &subscriber, &subscriber_len) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) {
    RETURN_NULL();
  }
  if(jlog_ctx_open_reader(jo->ctx, subscriber) != 0) {
    RETURN_NULL();
  }
  ZVAL_ADDREF(_this_zval);
  return_value = _this_zval;
}
/* }}} open */



/* {{{ proto string read()
 */
PHP_METHOD(Jlog_Reader, read)
{
  zend_class_entry * _this_ce;
  zval * _this_zval = NULL;
  jlog_obj *jo;
  const jlog_id epoch = { 0, 0 };
  jlog_id cur;
  jlog_message message;
  int cnt;

  if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &_this_zval, Jlog_Reader_ce_ptr) == FAILURE) {
    return;
  }

  _this_ce = Z_OBJCE_P(_this_zval);
  jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) {
    RETURN_FALSE;
  }
  /* if start is unset, we need to read the interval (again) */
  if(!memcmp(&jo->start, &epoch, sizeof(jlog_id)))
  {
    cnt = jlog_ctx_read_interval(jo->ctx, &jo->start, &jo->end);
    if(cnt == -1) {
      php_error(E_WARNING, "jlog_ctx_read_interval failed");
    }
    if(cnt == 0) {
      jo->start = epoch;
      jo->end = epoch;
      RETURN_FALSE;
    }
  }
  /* if last is unset, start at the beginning */
  if(!memcmp(&jo->last, &epoch, sizeof(jlog_id))) {
    cur = jo->start;
  } else {
    /* if we've already read the end, return; otherwise advance */
    if (!memcmp(&jo->last, &jo->end, sizeof(jlog_id))) {
      jo->start = epoch;
      jo->end = epoch;
      RETURN_FALSE;
    } else {
      cur = jo->last;
      JLOG_ID_ADVANCE(&cur);
    }
  }

  if(jlog_ctx_read_message(jo->ctx, &cur, &message) != 0) {
    php_error(E_WARNING, "read failed");
    RETURN_FALSE;
  }
  if(jo->auto_checkpoint) {
    if(jlog_ctx_read_checkpoint(jo->ctx, &cur) != 0) {
      php_error(E_WARNING, "checkpoint failed");
      RETURN_FALSE;
    }
    /* we have to re-read the interval after a checkpoint */
    jo->last = epoch;
    jo->start = epoch;
    jo->end = epoch;
  } else {
    /* update last */
    jo->last = cur;
    /* if we've reaached the end, clear interval so we'll re-read it */
    if(!memcmp(&jo->last, &jo->end, sizeof(jlog_id))) {
      jo->start = epoch;
      jo->end = epoch;
    }
  }
  RETURN_STRINGL(message.mess, message.mess_len, 1);
end:
  ;
}

/* }}} read */



/* {{{ proto object Jlog_Reader checkpoint()
   */
PHP_METHOD(Jlog_Reader, checkpoint)
{
  jlog_id epoch = { 0, 0 };
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O", &_this_zval, Jlog_Reader_ce_ptr) == FAILURE) {
		return;
	}

	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) { RETURN_NULL(); }
  if(memcmp(&jo->last, &epoch, sizeof(jlog_id)))
  {
    jlog_ctx_read_checkpoint(jo->ctx, &jo->last);
    /* we have to re-read the interval after a checkpoint */
    jo->last = epoch;
    jo->start = epoch;
    jo->end = epoch;
  }
  ZVAL_ADDREF(_this_zval);
  return_value = _this_zval;
}
/* }}} checkpoint */



/* {{{ proto bool auto_checkpoint([bool state])
   */
PHP_METHOD(Jlog_Reader, auto_checkpoint)
{
	zend_class_entry * _this_ce;
	zval * _this_zval = NULL;
	zend_bool state = 0;
    jlog_obj *jo;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O|b", &_this_zval, Jlog_Reader_ce_ptr, &state) == FAILURE) {
		return;
	}

  fprintf(stderr, "num_args = %d\n", ZEND_NUM_ARGS());
	_this_ce = Z_OBJCE_P(_this_zval);
	jo = (jlog_obj *) zend_object_store_get_object(_this_zval TSRMLS_CC);
  if(!jo || !jo->ctx) { RETURN_NULL(); }
  if(ZEND_NUM_ARGS() == 1) {
    jo->auto_checkpoint = state;
  }
  RETURN_LONG(jo->auto_checkpoint);
}
/* }}} auto_checkpoint */


static zend_function_entry Jlog_Reader_methods[] = {
	PHP_ME(Jlog_Reader, open, Jlog_Reader__open_args, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog_Reader, read, NULL, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog_Reader, checkpoint, NULL, /**/ZEND_ACC_PUBLIC)
	PHP_ME(Jlog_Reader, auto_checkpoint, Jlog_Reader__auto_checkpoint_args, /**/ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

/* }}} Methods */

static void class_init_Jlog_Reader(TSRMLS_D)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Jlog_Reader", Jlog_Reader_methods);
  ce.create_object = jlog_objects_new;
	Jlog_Reader_ce_ptr = zend_register_internal_class_ex(&ce, Jlog_ce_ptr, NULL TSRMLS_CC);
}

/* }}} Class Jlog_Reader */

/* }}} Class definitions*/

/* {{{ jlog_functions[] */
function_entry jlog_functions[] = {
	{ NULL, NULL, NULL }
};
/* }}} */


/* {{{ jlog_module_entry
 */
zend_module_entry jlog_module_entry = {
	STANDARD_MODULE_HEADER,
	"jlog",
	jlog_functions,
	PHP_MINIT(jlog),     /* Replace with NULL if there is nothing to do at php startup   */ 
	PHP_MSHUTDOWN(jlog), /* Replace with NULL if there is nothing to do at php shutdown  */
	PHP_RINIT(jlog),     /* Replace with NULL if there is nothing to do at request start */
	PHP_RSHUTDOWN(jlog), /* Replace with NULL if there is nothing to do at request end   */
	PHP_MINFO(jlog),
	"0.0.1", 
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_JLOG
ZEND_GET_MODULE(jlog)
#endif


/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(jlog)
{
    zend_object_handlers *std_hnd = zend_get_std_object_handlers();

    memcpy(&jlog_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    jlog_object_handlers.clone_obj = NULL;

	class_init_Jlog(TSRMLS_C);
	class_init_Jlog_Writer(TSRMLS_C);
	class_init_Jlog_Reader(TSRMLS_C);
    
    
	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(jlog)
{

	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(jlog)
{
	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(jlog)
{
	/* add your stuff here */

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(jlog)
{
	php_info_print_box_start(0);
	php_printf("<p>A sample PHP extension</p>\n");
	php_printf("<p>Version 0.0.1devel (2007-07-02)</p>\n");
	php_info_print_box_end();
	/* add your stuff here */

}
/* }}} */

#endif /* HAVE_JLOG */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=2 ts=2 sts=2 ai bs=2 fdm=marker
 */
