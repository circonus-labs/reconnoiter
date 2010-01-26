/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.broker.IMQBroker;
import com.omniti.reconnoiter.event.*;

public class EventHandler {
	
  private EPServiceProvider epService;
  private ConcurrentHashMap<UUID, StratconQueryBase> queries;
  private IMQBroker broker;

  public EventHandler(ConcurrentHashMap<UUID,StratconQueryBase> queries, EPServiceProvider epService, IMQBroker broker) {
    this.epService = epService;
    this.queries = queries;
    this.broker = broker;
  }
  public EPServiceProvider getService() { return epService; }
  public IMQBroker getBroker() { return broker; }
  public boolean registerQuery(StratconQueryBase sq) {
    if(queries.containsKey(sq.getUUID())) return false;
    queries.put(sq.getUUID(), sq);
    return true;
  }
  public boolean deregisterQuery(UUID id) {
    StratconQueryBase sq = queries.get(id);
    if(sq != null) {
      queries.remove(sq.getUUID());
      sq.destroy();
      System.err.println("Stopping Query/Statement: " + id);
      return true;
    }
    return false;
  }
  public boolean isQueryRegistered(UUID id) { return queries.containsKey(id); }

  public void processMessage(StratconMessage m) throws Exception {
    m.handle(this);
  }
  public void processMessage(StratconMessage[] messages) throws Exception {
    Exception last = null;
    for ( StratconMessage m : messages ) {
      if(m != null) try { processMessage(m); } catch (Exception e) { last = e; }
    }
    if(last != null) throw(last);
  }
  public void processMessage(String payload) throws Exception {
    Exception last = null;
    StratconMessage[] messages = StratconMessage.makeMessages(payload);
    if(messages == null) {
      System.err.println("Can't grok:\n" + payload);
    }
    processMessage(messages);
  }
}
