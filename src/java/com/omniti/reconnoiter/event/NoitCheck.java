package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;


public class NoitCheck extends StratconMessage {
   String uuid; 
   String target; 
   String module;
   String name; 
   String noit;

   protected String getPrefix() {
     return "C";
   }



   /*
    'C' REMOTE TIMESTAMP UUID TARGET MODULE NAME
    */
   public NoitCheck(String[] parts) throws Exception {
      super(parts);
      noit = parts[1];
      uuid = parts[3];
      target = parts[4];
      module = parts[5];
      name = parts[6];

   }

    public String getUuid() { return uuid; }
    public String getName() { return name; }
    public String getTarget() { return target; }
    public String getModule() { return module; }
    public String getNoit() { return noit; }

  protected int getLength() {
     return 7;
   }
}