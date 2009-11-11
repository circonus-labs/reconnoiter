/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.event.StratconQueryBase;
import com.omniti.reconnoiter.EventHandler;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.UpdateListener;

import java.util.UUID;

public class StratconStatement extends StratconQueryBase {

  public String getPrefix() {
    return "D";
  }

  /*  'D' REMOTE ID QUERY  */
  public StratconStatement() {}
  public StratconStatement(String[] parts) throws Exception {
    super(parts);
    String id = parts[2];
    expression = parts[3];
    if(id == null)
      uuid = UUID.randomUUID();
    else
      uuid = UUID.fromString(id);
  }

  public int getLength() {
    return 4;
  }

  public void handle(EventHandler eh) {
    eh.deregisterQuery(getUUID());
    EPStatement statement = eh.getService().getEPAdministrator().createEPL(getExpression());
    setStatement(statement);  
    if(eh.registerQuery(this)) {
      System.err.println("Creating Statement: " + getUUID());
    }
  }
}
