package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;

public class NoitMetricText extends StratconMessage {
   String uuid;
   Long time;
   String name;
   String message;
   String noit;

   protected String getPrefix() {
     return "M";
   }
   /*
   'M' REMOTE TIMESTAMP UUID NAME TYPE VALUE
    */
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


    protected int getLength() {
        return 7;
    }
}
