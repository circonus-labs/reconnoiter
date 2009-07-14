/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import java.lang.System;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import com.omniti.reconnoiter.MQListener;
import com.omniti.reconnoiter.broker.BrokerFactory;
import com.omniti.reconnoiter.event.NoitEvent;
import com.omniti.reconnoiter.StratconConfig;
import com.espertech.esper.client.*;
import org.apache.log4j.BasicConfigurator;

class IEPEngine {
  static public void main(String[] args) {
    BasicConfigurator.configure();
    if(args.length != 1) {
      System.err.println("Requires exactly one argument");
      return;
    }
    StratconConfig sconf = new StratconConfig(args[0]);

    Configuration config = new Configuration();
    config.addDatabaseReference("recondb", sconf.getDBConfig());
    config.addEventTypeAutoName("com.omniti.reconnoiter.event");
    EPServiceProvider epService = EPServiceProviderManager.getDefaultProvider(config);
    NoitEvent.registerTypes(epService);

    epService.getEPAdministrator().createEPL("create window CheckDetails.std:unique(uuid).win:keepall() as NoitCheck");
    epService.getEPAdministrator().createEPL("insert into CheckDetails select * from NoitCheck");

    MQListener l = new MQListener(epService, BrokerFactory.getBroker(sconf));

    Thread listener = new Thread(l);
    listener.start();

    BufferedReader stdin = new BufferedReader(new InputStreamReader(System.in));
    while(true) {
      try {
        if(stdin.readLine() == null) throw new Exception("all done");
      } catch(Exception e) {
        System.exit(-1);
      }
    }
  }
}
