package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.NoitMetricNumeric;
import com.omniti.reconnoiter.event.NoitMetricText;

public class NoitMetric extends NoitEvent {
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
    if(nmn != null) eh.getService().getEPRuntime().sendEvent(nmn);
    if(nmt != null) eh.getService().getEPRuntime().sendEvent(nmt);
  }
  public String getPrefix() { return "M"; }
  public int getLength() { return 7; }
}
