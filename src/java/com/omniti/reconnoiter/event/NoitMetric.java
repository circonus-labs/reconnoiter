/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.NoitMetricNumeric;
import com.omniti.reconnoiter.event.NoitMetricText;
import org.apache.log4j.Logger;

public class NoitMetric extends NoitEvent {
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
  public void handle(EventHandler eh) {
    long start = System.nanoTime();
    if(nmn != null) eh.getService().getEPRuntime().sendEvent(nmn);
    if(nmt != null) eh.getService().getEPRuntime().sendEvent(nmt);
    long nanos = System.nanoTime() - start;
    logger.debug("sendEvent("+getUuid()+"-"+getName()+") took "+(nanos/1000)+"us");
  }
  public String getUuid() {
    return (nmn != null) ? nmn.getUuid() : nmt.getUuid();
  }
  public String getName() {
    return (nmn != null) ? nmn.getName() : nmt.getName();
  }
  public boolean isNumeric() { return nmn != null; }
  public boolean isText() { return nmt != null; }
  public String getPrefix() { return "M"; }
  public int getLength() { return 7; }
}
