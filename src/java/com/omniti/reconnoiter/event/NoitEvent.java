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

public abstract class NoitEvent extends StratconMessage {
  public NoitEvent() {}
  public NoitEvent(String[] parts) throws Exception {
    super(parts);
  }
  public void handle(EventHandler eh) {
    eh.getService().getEPRuntime().sendEvent(this);
  }
}
