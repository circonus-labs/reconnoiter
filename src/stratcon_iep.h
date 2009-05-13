/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_STRATCON_IEP_H
#define _NOIT_STRATCON_IEP_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_hash.h"
#include "stratcon_datastore.h"

#include <sys/types.h>
#include <sys/socket.h>

API_EXPORT(void)
  stratcon_iep_init();

API_EXPORT(void)
  stratcon_iep_line_processor(stratcon_datastore_op_t op,
                              struct sockaddr *remote, void *operand);

#endif
