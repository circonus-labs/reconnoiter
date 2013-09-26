/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.IEventHandler;
import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.NoitMetricNumeric;
import com.omniti.reconnoiter.event.NoitMetricText;
import org.apache.log4j.Logger;

public class NoitMetric extends NoitEvent 
       implements NoitMetricGeneric {
  static Logger logger = Logger.getLogger(NoitMetric.class.getName());
  public final static String METRIC_STRING = "s";

  private NoitMetricText nmt;
  private NoitMetricNumeric nmn;
 
  public NoitMetric() {}
  public NoitMetric(String[] parts) throws Exception {
    super(parts);
    if (parts[5].equals(METRIC_STRING)) {
      nmn = null;
      nmt = new NoitMetricText(parts);
    }
    else {
      nmn = new NoitMetricNumeric(parts);
      nmt = null;
    }
  }
  public void handle(IEventHandler eh) {
    long start = System.nanoTime();
    if(nmn != null) eh.sendEvent(nmn);
    if(nmt != null) eh.sendEvent(nmt);
    long nanos = System.nanoTime() - start;
    logger.debug("sendEvent("+getUuid()+"-"+getName()+") took "+(nanos/1000)+"us");
  }
  public String getUuid() { return (nmn != null)?nmn.getUuid():nmt.getUuid(); }
  public String getName() { return (nmn != null)?nmn.getName():nmt.getName(); }
  public long getTime() { return (nmn != null)?nmn.getTime():nmt.getTime(); }
  public String getNoit() { return (nmn != null)?nmn.getNoit():nmt.getNoit(); }
  public String getCheck_target() { return (nmn != null)?nmn.getCheck_target():nmt.getCheck_target(); }
  public String getCheck_module() { return (nmn != null)?nmn.getCheck_module():nmt.getCheck_module(); }
  public String getCheck_name() { return (nmn != null)?nmn.getCheck_name():nmt.getCheck_name(); }

  public boolean isNumeric() { return nmn != null; }
  public boolean isText() { return nmt != null; }
  public NoitMetricNumeric getNumeric() { return nmn; }
  public NoitMetricText getText() { return nmt; }
  public String getPrefix() { return "M"; }
  public int numparts() { return 7; }
}
