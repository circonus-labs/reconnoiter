package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;


public class NoitStatus  extends StratconMessage {
   String uuid;
   String status;
   String state;
   String availability;
   Double duration;
   String noit;

  @Override
  protected String getPrefix() {
     return "S";
   }

   /*
    'S' REMOTE TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
    */
   public NoitStatus(String[] parts) throws Exception {
      super(parts);
      noit = parts[1];
      uuid = parts[3];
      state = parts[4];
      availability = parts[5];
      duration = Double.parseDouble(parts[6]);
      status = parts[7];

   }

    public String getUuid() { return uuid; }
    public String getStatus() { return status; }
    public String getState() { return state; }
    public String getAvailability() { return availability; }
    public Double getDuration() { return duration; }
    public String getNoit() { return noit; }

  protected int getLength() {
     return 8;
   }
}