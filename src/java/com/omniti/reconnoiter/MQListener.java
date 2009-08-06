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
import java.lang.Runnable;

import com.espertech.esper.client.EPServiceProvider;
import java.util.concurrent.ConcurrentHashMap;
import java.util.UUID;

public class MQListener implements Runnable {
    private EPServiceProvider epService;
    private ConcurrentHashMap<UUID,StratconQueryBase> queries;
    private IMQBroker broker;

    public MQListener(EPServiceProvider epService, IMQBroker broker) {
      this.queries = new ConcurrentHashMap<UUID,StratconQueryBase>();
      this.epService = epService;
      this.broker = broker;
      broker.connect();
    }
    
    public void run() {
      EventHandler eh = new EventHandler(queries, this.epService, broker);
      broker.consume(eh);
    }
}
