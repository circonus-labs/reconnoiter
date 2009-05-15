
#include "noit_xml.h"

struct noit_xml_buffer_ptr {
  char *buff;
  int raw_len;
  int len;
  int allocd;
};
static int
noit_xml_save_writer(void *vstr, const char *buffer, int len) {
  struct noit_xml_buffer_ptr *clv = vstr;
  if(!clv->buff) {
    clv->allocd = 8192;
    clv->buff = malloc(clv->allocd);
  }
  while(len + clv->len > clv->allocd) {
    char *newbuff;
    int newsize = clv->allocd;
    newsize <<= 1;
    newbuff = realloc(clv->buff, newsize);
    if(!newbuff) {
      return -1;
    }
    clv->allocd = newsize;
    clv->buff = newbuff;
  }
  memcpy(clv->buff + clv->len, buffer, len);
  clv->len += len;
  return len;
}
static int
noit_xml_save_closer(void *vstr) {
  struct noit_xml_buffer_ptr *clv = vstr;
  if(clv->buff == NULL) return 0;
  clv->buff[clv->len] = '\0';
  return 0;
}

char *
noit_xmlSaveToBuffer(xmlDocPtr doc) {
  char *outbuff;
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct noit_xml_buffer_ptr *buf;

  buf = calloc(1, sizeof(*buf));
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_xml_save_writer,
                                noit_xml_save_closer,
                                buf, enc);
  xmlSaveFormatFileTo(out, doc, "utf8", 1);
  outbuff = buf->buff;
  free(buf);
  return outbuff;
}

