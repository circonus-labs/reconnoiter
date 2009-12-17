/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
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

  public String getPrefix() {
    return "S";
  }

  /*
   'S' REMOTE TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
   */
  public NoitStatus() {}
  public NoitStatus(String[] parts) throws Exception {
    super(parts);
    noit = parts[1];
    uuid = parts[3];
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

  public int getLength() {
    return 8;
  }
}
