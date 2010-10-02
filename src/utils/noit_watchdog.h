/*
 * Copyright (c) 2005-2007, OmniTI Computer Consulting, Inc.
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
 *    * Neither the name OmniTI Computer Consulting, Inc. nor the names
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

#ifndef _NOIT_WATCHDOG_H
#define _NOIT_WATCHDOG_H

#include "noit_config.h"
#include "noit_defines.h"

/*! \fn int noit_watchdog_prefork_init()
    \brief Prepare the program to split into a child/parent-monitor relationship.
    \return Returns zero on success.
.
    
    noit_watchdog_prefork_init sets up the necessary plumbing to bridge across a
child to instrument watchdogs.
 */
API_EXPORT(int)
  noit_watchdog_prefork_init();

/*! \fn int noit_watchdog_start_child(const char *app, int (*func)(), int timeout)
    \brief Starts a function as a separate child under close watch.
    \param app The name of the application (for error output).
    \param func The function that will be the child process.
    \param timeout The number of seconds of lifelessness before the parent reaps and restarts the child.
    \return Returns on program termination.
.
    
    noit_watchdog_start_child will fork and run the specified function in the child process.  The parent will watch.  The child process must initialize the eventer system and then call noit_watchdog_child_hearbeat to let the parent know it is alive.  If the eventer system is being used to drive the child process, noit_watchdog_child_eventer_heartbeat may be called once after the eventer is initalized.  This will induce a regular heartbeat.
 */
API_EXPORT(int)
  noit_watchdog_start_child(const char *app, int (*func)(), int timeout);

/*! \fn int noit_watchdog_child_heartbeat()
    \return Returns zero on success

    noit_watchdog_child_heartbeat is called within the child function to alert the parent that the child is still alive and functioning correctly.
 */
API_EXPORT(int)
  noit_watchdog_child_heartbeat();

/*! \fn int noit_watchdog_child_eventer_heartbeat()
    \return Returns zero on success

    noit_watchdog_child_eventer_heartbeat registers a periodic heartbeat through the eventer subsystem.  The eventer must be initialized before calling this function.
 */
API_EXPORT(int)
  noit_watchdog_child_eventer_heartbeat();

API_EXPORT(void)
  noit_watchdog_glider(const char *path);

API_EXPORT(void)
  noit_watchdog_glider_trace_dir(const char *path);

#endif
