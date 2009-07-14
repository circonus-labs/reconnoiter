package com.omniti.reconnoiter.broker;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.event.StratconQuery;

public interface IMQBroker {
  
  public void connect();
  public void consume(EventHandler eh);
  public UpdateListener getListener(EPServiceProvider epService, StratconQuery sq);

}
