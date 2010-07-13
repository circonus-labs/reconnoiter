package com.omniti.reconnoiter;

import com.omniti.reconnoiter.StratconMessage;

public interface MessageHandler {
  public boolean observe(StratconMessage m);
}
