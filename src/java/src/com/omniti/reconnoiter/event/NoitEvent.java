/*
 * Copyright (c) 2013-2017, Circonus, Inc. All rights reserved.
 * Copyright (c) 2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
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

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.IEventHandler;
import com.omniti.reconnoiter.StratconMessage;

public abstract class NoitEvent extends StratconMessage {
  public NoitEvent() {}
  public NoitEvent(String[] parts) throws Exception {
    super(parts);
  }
  public String[] extended_id_split(String id) {
    String parts[] = { null, null, null, null }; /* target,module,name,uuid */
    if(id.length() <= 36) {
      parts[3] = id;
      return parts;
    }
    parts[3] = id.substring(id.length() - 36);
    int p1 = id.indexOf('`');
    if(p1 < 0) return parts;
    parts[0] = id.substring(0,p1);
    int p2 = id.indexOf('`', p1+1);
    if(p2 < 0) return parts;
    parts[1] = id.substring(p1+1,p2);
    parts[2] = id.substring(p2+1,id.length()-37);
    return parts;
  }
  public void handle(IEventHandler eh) {
    eh.sendEvent(this);
  }
}
