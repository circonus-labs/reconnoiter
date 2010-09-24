/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.event.StratconQuery;
import java.io.IOException;

public interface IMQBroker {
  
  public void connect() throws Exception;
  public void disconnect();
  public void consume(EventHandler eh) throws IOException;
  public UpdateListener getListener(EPServiceProvider epService, StratconQuery sq);

}
