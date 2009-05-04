/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_STRATCON_DATASTORE_H
#define _NOIT_STRATCON_DATASTORE_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_hash.h"

#include <sys/types.h>
#include <sys/socket.h>

typedef enum {
 DS_OP_INSERT = 1,
 DS_OP_CHKPT = 2,
 DS_OP_FIND = 3,
 DS_OP_FIND_COMPLETE = 4
} stratcon_datastore_op_t;

API_EXPORT(void)
  stratcon_datastore_push(stratcon_datastore_op_t,
                          struct sockaddr *, void *);

API_EXPORT(void)
  stratcon_datastore_register_onlooker(void (*f)(stratcon_datastore_op_t,
                                                 struct sockaddr *, void *));

API_EXPORT(int)
  stratcon_datastore_saveconfig(void *unused);

#endif
