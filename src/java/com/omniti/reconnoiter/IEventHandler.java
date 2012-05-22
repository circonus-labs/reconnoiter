/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import com.omniti.reconnoiter.broker.IMQBroker;
import com.omniti.reconnoiter.event.*;
import com.omniti.reconnoiter.MessageHandler;

public interface IEventHandler {
  public void addObserver(MessageHandler mh);
  public IMQBroker getBroker();
  public void processMessage(StratconMessage m) throws Exception;
  public void processMessage(String payload) throws Exception;
  public void sendEvent(StratconMessage m);
  public boolean stopProcessing(StratconMessage m, String source);
  public long getNumEventsHandled();
  public long getMicrosecondsHandlingEvents();
}
