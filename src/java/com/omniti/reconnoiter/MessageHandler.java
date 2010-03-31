package com.omniti.reconnoiter;

import com.omniti.reconnoiter.StratconMessage;

public interface MessageHandler {
  public void observe(StratconMessage m);
}
