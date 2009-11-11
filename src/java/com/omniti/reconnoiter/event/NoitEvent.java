package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconMessage;

public abstract class NoitEvent extends StratconMessage {
  public NoitEvent() {}
  public NoitEvent(String[] parts) throws Exception {
    super(parts);
  }
  public void handle(EventHandler eh) {
    eh.getService().getEPRuntime().sendEvent(this);
  }
}
