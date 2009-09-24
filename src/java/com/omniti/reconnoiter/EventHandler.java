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
	
  public void processMessage(String xml) throws Exception {
    StratconMessage m = StratconMessage.makeMessage(xml);
    if(m == null) {
      System.err.println("Can't grok:\n" + xml);
    }
    if(m instanceof StratconStatement) {
      StratconStatement sq = (StratconStatement) m;

      if(queries.containsKey(sq.getUUID())) throw (new Exception("Duplicate Query"));

      EPStatement statement = epService.getEPAdministrator().createEPL(sq.getExpression());
      sq.setStatement(statement);
      queries.put(sq.getUUID(), sq);
      System.err.println("Creating Statement: " + sq.getUUID());
    }
    else if(m instanceof StratconQuery) {
      StratconQuery sq = (StratconQuery) m;

      if(queries.containsKey(sq.getUUID())) throw (new Exception("Duplicate Query"));

      EPStatement statement = epService.getEPAdministrator().createEPL(sq.getExpression());
      UpdateListener o = broker.getListener(this.epService, sq);

      statement.addListener(o);
      sq.setStatement(statement);
      sq.setListener(o);
      queries.put(sq.getUUID(), sq);
      System.err.println("Creating Query: " + sq.getUUID());
    }
    else if(m instanceof StratconQueryStop) {
      /* QueryStop stops both queries and statements */
      StratconQueryBase sq = queries.get(((StratconQueryStop) m).getUUID());
      if(sq != null) {
        queries.remove(sq.getUUID());
        sq.destroy();
      }
    }
     else if(m instanceof NoitMetricText) {
      epService.getEPRuntime().sendEvent(m);
    }
    else if(m instanceof NoitMetricNumeric) {
      epService.getEPRuntime().sendEvent(m);
    }
    else if(m instanceof NoitCheck) {
      epService.getEPRuntime().sendEvent(m);
    }
    else if(m instanceof NoitStatus) {
      epService.getEPRuntime().sendEvent(m);
    }
	}
}
