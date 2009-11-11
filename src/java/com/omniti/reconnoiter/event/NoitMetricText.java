package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.event.NoitEvent;

public class NoitMetricText extends NoitEvent {
   String uuid;
   Long time;
   String name;
   String message;
   String noit;

   public String getPrefix() {
     return "M";
   }
   /*
   'M' REMOTE TIMESTAMP UUID NAME TYPE VALUE
    */
    public NoitMetricText() {}
    public NoitMetricText(String[] parts) throws Exception {
      super(parts);
      noit = parts[1];
      uuid = parts[3];
      name = parts[4];
      message = parts[6].equals("[[null]]") ? null : parts[6];
      time = timeToLong(parts[2]);
    }
    public String getUuid() { return uuid; }
    public Long getTime() { return time; }
    public String getName() { return name; }
    public String getMessage() { return message; }
    public String getNoit() { return noit; }


    public int getLength() {
        return 7;
    }
}
