package com.omniti.reconnoiter;

import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.broker.IMQBroker;
import com.omniti.reconnoiter.event.NoitEvent;
import com.omniti.reconnoiter.event.NoitMetricNumeric;
import com.omniti.reconnoiter.event.StratconQuery;
import com.omniti.reconnoiter.event.StratconQueryStop;

public class EventHandler {
	
	private EPServiceProvider epService;
	private ConcurrentHashMap<UUID, StratconQuery> queries;
	private IMQBroker broker;

	public EventHandler(ConcurrentHashMap<UUID,StratconQuery> queries, EPServiceProvider epService, IMQBroker broker) {
		this.epService = epService;
		this.queries = queries;
		this.broker = broker;
	}
	
	public void processMessage(String xml) throws Exception {
		
		StratconMessage m = StratconMessage.makeMessage(xml);
    if(m == null) {
      System.err.println("Can't grok:\n" + xml);
    }
    if(m instanceof StratconQuery) {
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
      StratconQuery sq = queries.get(((StratconQueryStop) m).getUUID());
      if(sq != null) {
        queries.remove(sq.getUUID());
        sq.destroy();
      }
    }
    else if(m instanceof NoitEvent) {
      epService.getEPRuntime().sendEvent(((NoitEvent) m).getDocument());
    }
    else if(m instanceof NoitMetricNumeric) {
      epService.getEPRuntime().sendEvent((NoitMetricNumeric) m);
    }
	}
}
