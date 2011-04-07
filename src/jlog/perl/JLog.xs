#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"
#include "jlog.h"

typedef struct {
  jlog_ctx *ctx;
  char *path;
  jlog_id start;
  jlog_id last;
  jlog_id prev;
  jlog_id end;
  int auto_checkpoint;
  int error;
} jlog_obj;

typedef jlog_obj * JLog;
typedef jlog_obj * JLog_Writer;
typedef jlog_obj * JLog_Reader;

#define FREE_JLOG_OBJ(my_obj) do { \
  if(my_obj->ctx) { \
    jlog_ctx_close(my_obj->ctx); \
  } \
  if(my_obj->path){ \
    free(my_obj->path); \
  } \
  free(my_obj); \
} while(0)

#define SYS_CROAK(message) do { \
  croak(message "; error: %d (%s) errno: %d (%s)", \
    jlog_ctx_err(my_obj->ctx), jlog_ctx_err_string(my_obj->ctx), \
    jlog_ctx_errno(my_obj->ctx), strerror(jlog_ctx_errno(my_obj->ctx))); \
} while (0)

MODULE = JLog		PACKAGE = JLog PREFIX=JLOG_

SV *JLOG_new(classname, path, ...)
  char *classname;
  char *path;
  CODE:
    {
      jlog_obj *my_obj;
      int options = O_CREAT;
      size_t size = 0;
      my_obj = calloc(1, sizeof(*my_obj));
      my_obj->ctx = jlog_new(path);
      my_obj->path = strdup(path);
      if(items > 2) {
        options = SvIV(ST(2));
        if(items > 3) {
          size = SvIV(ST(3));
        }
      }

      if(!my_obj->ctx) { 
        FREE_JLOG_OBJ(my_obj);
        croak("jlog_new(%s) failed", path);
      }
      if(options & O_CREAT) {
        if(size) {
          jlog_ctx_alter_journal_size(my_obj->ctx, size);
        }
        if(jlog_ctx_init(my_obj->ctx) != 0) {
          if(jlog_ctx_err(my_obj->ctx) == JLOG_ERR_CREATE_EXISTS) {
            if(options & O_EXCL) {
              FREE_JLOG_OBJ(my_obj);
              croak("file already exists: %s", path);
            }
          } else {
            int err = jlog_ctx_err(my_obj->ctx);
            const char *err_string = jlog_ctx_err_string(my_obj->ctx);
            FREE_JLOG_OBJ(my_obj);
            croak("error initializing jlog: %d %s", err, err_string);
          }
        }
        jlog_ctx_close(my_obj->ctx);
        my_obj->ctx = jlog_new(path);
        if(!my_obj->ctx) { 
          FREE_JLOG_OBJ(my_obj);
          croak("jlog_new(%s) failed after successful init", path);
        }
      }
      RETVAL = newSV(0);
      sv_setref_pv(RETVAL, classname, (void *)my_obj);
    }
  OUTPUT:
    RETVAL

SV *JLOG_JLOG_BEGIN()
  CODE:
    {
      RETVAL = newSViv(JLOG_BEGIN);
    }
  OUTPUT:
    RETVAL
    
SV *JLOG_JLOG_END()
  CODE:
    {
      RETVAL = newSViv(JLOG_END);
    }
  OUTPUT:
    RETVAL


SV *JLOG_add_subscriber(my_obj, subscriber, ...)
  JLog my_obj;
  char *subscriber;
  CODE:
    {
      int whence = JLOG_BEGIN;
      if(items > 2) {
        whence = SvIV(ST(2));
      }
      if(!my_obj || !my_obj->ctx ||
         jlog_ctx_add_subscriber(my_obj->ctx, subscriber, whence) != 0) 
      {
        RETVAL = &PL_sv_no;
      } else {
        RETVAL = &PL_sv_yes;
      }
    }
  OUTPUT:
    RETVAL

SV *JLOG_remove_subscriber(my_obj, subscriber)
  JLog my_obj;
  char *subscriber;
  CODE:
    {
      if(!my_obj || !my_obj->ctx ||
         jlog_ctx_remove_subscriber(my_obj->ctx, subscriber) != 0) 
      {
        RETVAL = &PL_sv_no;
      } else {
        RETVAL = &PL_sv_yes;
      }
    }
  OUTPUT:
    RETVAL

void JLOG_list_subscribers(my_obj)
  JLog my_obj;
  PPCODE:
    {
      char **list;
      int i;
      if(!my_obj || !my_obj->ctx) {
        croak("invalid jlog context");
      }
      jlog_ctx_list_subscribers(my_obj->ctx, &list);
      for(i=0; list[i]; i++) {
        XPUSHs(sv_2mortal(newSVpv(list[i], 0)));
      }
      jlog_ctx_list_subscribers_dispose(my_obj->ctx, list);
    }

SV *JLOG_alter_journal_size(my_obj, size)
  JLog my_obj;
  size_t size;
  CODE:
    {
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      /* calling jlog_ctx_alter_journal_size here will never have any
       * effect, it's either too late or too early. Make this return
       * failure and deprecate it */
      RETVAL = &PL_sv_no;
    }
  OUTPUT:
    RETVAL

SV *JLOG_raw_size(my_obj)
  JLog my_obj;
  CODE:
    {
      size_t size;
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      size = jlog_raw_size(my_obj->ctx);
      RETVAL = newSViv(size);
    }
  OUTPUT:
    RETVAL
  
void JLOG_close(my_obj)
  JLog my_obj;
  CODE:
    {
      if(!my_obj || !my_obj->ctx) { return; }
      jlog_ctx_close(my_obj->ctx);
      my_obj->ctx = NULL;
    }

SV* JLOG_inspect(my_obj)
  JLog my_obj;
  CODE:
    {
      HV *rh;
      char start[20], last[20], prev[20], end[20];
      rh = (HV *)sv_2mortal((SV *)newHV());
      jlog_snprint_logid(start, sizeof(start), &my_obj->start);
      hv_store(rh, "start", sizeof("start") - 1, newSVpv(start, 0), 0);

      jlog_snprint_logid(last, sizeof(last), &my_obj->last);
      hv_store(rh, "last", sizeof("last") - 1, newSVpv(last, 0), 0);

      jlog_snprint_logid(prev, sizeof(prev), &my_obj->prev);
      hv_store(rh, "prev", sizeof("prev") - 1, newSVpv(prev, 0), 0);

      jlog_snprint_logid(end, sizeof(end), &my_obj->end);
      hv_store(rh, "end", sizeof("end") - 1, newSVpv(end, 0), 0);

      hv_store(rh, "path", sizeof("path") - 1, newSVpv(my_obj->path, 0), 0);
      RETVAL = newRV((SV *)rh);
    }
  OUTPUT:
    RETVAL

void JLOG_DESTROY(my_obj)
  JLog my_obj;
  CODE:
    {
      if(!my_obj) return;
      FREE_JLOG_OBJ(my_obj);
    }


MODULE = JLog PACKAGE = JLog::Writer PREFIX=JLOG_W_


SV *JLOG_W_open(my_obj)
  JLog_Writer my_obj;
  CODE:
    {
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      if(jlog_ctx_open_writer(my_obj->ctx) != 0) {
        SYS_CROAK("jlog_ctx_open_writer failed");
      } else {
        RETVAL = newSVsv(ST(0));
      }
    }
  OUTPUT:
    RETVAL

SV *JLOG_W_write(my_obj, buffer_sv, ...)
  JLog_Writer my_obj;
  SV *buffer_sv;
  CODE:
    {
      char *buffer;
      int ts = 0;
      jlog_message m;
      struct timeval t;
      STRLEN buffer_len;

      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      if(items > 2) {
        ts = (time_t) SvIV(ST(2));
      }

      buffer = SvPVx(buffer_sv, buffer_len);
      m.mess = buffer;
      m.mess_len = buffer_len;
      t.tv_sec = ts;
      t.tv_usec = 0;

      if(jlog_ctx_write_message(my_obj->ctx, &m, ts?&t:NULL) < 0) {
        RETVAL = &PL_sv_no;
      } else {
        RETVAL = &PL_sv_yes;
      }
    }
  OUTPUT:
    RETVAL


MODULE = JLog PACKAGE = JLog::Reader PREFIX=JLOG_R_


SV *JLOG_R_open(my_obj, subscriber)
  JLog_Reader my_obj;
  char *subscriber;
  CODE:
    {
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      if(jlog_ctx_open_reader(my_obj->ctx, subscriber) != 0) {
        SYS_CROAK("jlog_ctx_open_reader failed");
      } else {
        RETVAL = newSVsv(ST(0));
      }
    }
  OUTPUT:
    RETVAL

SV * JLOG_R_read(my_obj)
  JLog_Reader my_obj;
  CODE:
    {
      const jlog_id epoch = { 0, 0 };
      jlog_id cur;
      jlog_message message;
      int cnt;
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      /* if start is unset, we need to read the interval (again) */
      if(my_obj->error || !memcmp(&my_obj->start, &epoch, sizeof(jlog_id))) 
      {
        my_obj->error = 0;
        cnt = jlog_ctx_read_interval(my_obj->ctx, &my_obj->start, &my_obj->end);
        if(cnt == 0 || (cnt == -1 && jlog_ctx_err(my_obj->ctx) == JLOG_ERR_FILE_OPEN)) {
          my_obj->start = epoch;
          my_obj->end = epoch;
          RETVAL = &PL_sv_undef;
          goto end;
        }
        else if(cnt == -1) SYS_CROAK("jlog_ctx_read_interval failed");
      }
      /* if last is unset, start at the beginning */
      if(!memcmp(&my_obj->last, &epoch, sizeof(jlog_id))) {
        cur = my_obj->start;
      } else {
        /* if we've already read the end, return; otherwise advance */
        cur = my_obj->last;
        if(!memcmp(&my_obj->prev, &my_obj->end, sizeof(jlog_id))) {
          my_obj->start = epoch;
          my_obj->end = epoch;
          RETVAL = &PL_sv_undef;
          goto end;
        }
        jlog_ctx_advance_id(my_obj->ctx, &my_obj->last, &cur, &my_obj->end);
        if(!memcmp(&my_obj->last, &cur, sizeof(jlog_id))) {
	  my_obj->start = epoch;
	  my_obj->end = epoch;
	  RETVAL = &PL_sv_undef;
	  goto end;
        }
      }
      if(jlog_ctx_read_message(my_obj->ctx, &cur, &message) != 0) {
        if(jlog_ctx_err(my_obj->ctx) == JLOG_ERR_FILE_OPEN) {
	  my_obj->error = 1;
          RETVAL = &PL_sv_undef;
          goto end;
	}
        /* read failed; croak, but recover if the read is retried */
	my_obj->error = 1;
        SYS_CROAK("read failed");
      }
      if(my_obj->auto_checkpoint) {
        if(jlog_ctx_read_checkpoint(my_obj->ctx, &cur) != 0)
          SYS_CROAK("checkpoint failed");
        /* we have to re-read the interval after a checkpoint */
        my_obj->last = epoch;
        my_obj->prev = epoch;
        my_obj->start = epoch;
        my_obj->end = epoch;
      } else {
        /* update last */
        my_obj->prev = my_obj->last;
        my_obj->last = cur;
        /* if we've reaached the end, clear interval so we'll re-read it */
      }
      RETVAL = newSVpv(message.mess, message.mess_len);
end:
      ;
    }
  OUTPUT:
    RETVAL

SV *JLOG_R_rewind(my_obj)
  JLog_Reader my_obj;
  CODE:
    {
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      my_obj->last = my_obj->prev;
      RETVAL = newSVsv(ST(0));
    }
  OUTPUT:
    RETVAL

SV *JLOG_R_checkpoint(my_obj)
  JLog_Reader my_obj;
  CODE:
    {
      jlog_id epoch = { 0, 0 };
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      if(memcmp(&my_obj->last, &epoch, sizeof(jlog_id))) 
      {
        jlog_ctx_read_checkpoint(my_obj->ctx, &my_obj->last);
        /* we have to re-read the interval after a checkpoint */
        my_obj->last = epoch;
        my_obj->start = epoch;
        my_obj->end = epoch;
      }
      RETVAL = newSVsv(ST(0));
    }
  OUTPUT:
    RETVAL

SV *JLOG_R_auto_checkpoint(my_obj, ...)
  JLog_Reader my_obj;
  CODE:
    {
      if(!my_obj || !my_obj->ctx) { 
        croak("invalid jlog context");
      }
      if(items > 1) {
        int ac = SvIV(ST(1));
        my_obj->auto_checkpoint = ac;
      }
      RETVAL = newSViv(my_obj->auto_checkpoint);
    }
  OUTPUT:
    RETVAL
