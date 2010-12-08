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

  public int getLength() {
    return 8;
  }
}
