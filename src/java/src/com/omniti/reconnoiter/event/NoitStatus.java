/*
 * Copyright (c) 2013, Circonus, Inc. All rights reserved.
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

import com.omniti.reconnoiter.event.NoitEvent;

public class NoitStatus extends NoitEvent {
  String uuid;
  Long time;
  String status;
  String state;
  String availability;
  Double duration;
  String noit;
  String check_target;
  String check_module;
  String check_name;

  public String getPrefix() {
    return "S";
  }

  /*
   'S' REMOTE TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
   */
  public NoitStatus() {}
  public NoitStatus(String[] parts) throws Exception {
    super(parts);
    String id[] = extended_id_split(parts[3]);
    noit = parts[1];
    check_target = id[0];
    check_module = id[1];
    check_name = id[2];
    uuid = id[3];
    state = parts[4];
    availability = parts[5];
    duration = Double.parseDouble(parts[6]);
    status = parts[7];
    time = timeToLong(parts[2]);
  }

  public String getUuid() { return uuid; }
  public Long getTime() { return time; }
  public String getStatus() { return status; }
  public String getState() { return state; }
  public String getAvailability() { return availability; }
  public Double getDuration() { return duration; }
  public String getNoit() { return noit; }
  public String getCheck_target() { return check_target; }
  public String getCheck_module() { return check_module; }
  public String getCheck_name() { return check_name; }

  public int numparts() { return 8; }
}
