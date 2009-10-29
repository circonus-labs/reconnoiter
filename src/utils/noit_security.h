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

#ifndef _NOIT_SECURITY_H
#define _NOIT_SECURITY_H

/*! \fn int noit_security_chroot(const char *path)
    \brief chroot(2) to the specified directory.
    \param path The path to chroot to.
    \return Zero is returned on success.
    
    noit_security_chroot placing the calling application into a chroot
    environment.
 */
API_EXPORT(int) noit_security_chroot(const char *path);

/*! \fn int noit_security_usergroup(const char *user, const char *group,
                                    noit_boolean effective)
    \brief change the effective or real, effective and saved user and group
    \param user The user name as either a login or a userid in string form.
    \param group The group name as either a login or a groupid in string form.
    \param effective If true then only effective user and group are changed.
    \return Zero is returned on success.

    noit_security_usergroup will change the real, effective, and saved
    user and group for the calling process.  This is thread-safe.
 */
API_EXPORT(int) noit_security_usergroup(const char *user, const char *group,
                                        noit_boolean effective);

#endif
