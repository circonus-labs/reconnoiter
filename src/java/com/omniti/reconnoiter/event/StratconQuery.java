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

public class StratconQuery extends StratconQueryBase {
  protected boolean active;
  protected UpdateListener listener;
  protected String name;
  protected Thread thr;

  public String getPrefix() {
     return "Q";
   }

  /*  'Q' REMOTE ID NAME QUERY  */
  public StratconQuery() {}
  public StratconQuery(String[] parts) throws Exception {
    super(parts);
    String id = parts[2];
    name = parts[3];
    expression = parts[4];
    if(id == null)
      uuid = UUID.randomUUID();
    else
      uuid = UUID.fromString(id);

    if(name == null) name = "default";
    if(uuid == null) uuid = UUID.randomUUID();
    active = true;
  }
  public boolean isActive() {
    return active;
  }
  public String getName() {
    return name;
  }
  public void setThread(Thread thr) {
    this.thr = thr;
  }
  public void setListener(UpdateListener l) {
    this.listener = l;
  }
  public void destroy() {
    statement.removeListener(listener);
    statement.destroy();
    active = false;
    if(thr != null) thr.interrupt();
  }

  public int getLength() {
    return 5;
  }

  public void handle(EventHandler eh) {
    eh.deregisterQuery(getUUID());

    EPStatement statement = eh.getService().getEPAdministrator().createEPL(getExpression());
    UpdateListener o = eh.getBroker().getListener(eh.getService(), this);

    statement.addListener(o);
    setStatement(statement);
    setListener(o);
    eh.registerQuery(this);
    System.err.println("Creating Query: " + getUUID());
  }
}
