/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.event.NoitEvent;

public class NoitMetricText extends NoitEvent {
   String uuid;
   Long time;
   String name;
   String message;
   String noit;
   String check_target;
   String check_module;
   String check_name;

   public String getPrefix() {
     return "M";
   }
   /*
   'M' REMOTE TIMESTAMP UUID NAME TYPE VALUE
    */
    public NoitMetricText() {}
    public NoitMetricText(String[] parts) throws Exception {
      super(parts);
      String id[] = extended_id_split(parts[3]);
      noit = parts[1];
      check_target = id[0];
      check_module = id[1];
      check_name = id[2];
      uuid = id[3];
      name = parts[4];
      message = parts[6].equals("[[null]]") ? null : parts[6];
      time = timeToLong(parts[2]);
    }
    public String getUuid() { return uuid; }
    public Long getTime() { return time; }
    public String getName() { return name; }
    public String getMessage() { return message; }
    public String getNoit() { return noit; }
    public String getCheck_target() { return check_target; }
    public String getCheck_module() { return check_module; }
    public String getCheck_name() { return check_name; }

    public int getlength() { return 7; }
}
