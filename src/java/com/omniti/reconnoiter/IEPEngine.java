package com.omniti.reconnoiter;

import java.lang.System;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import com.omniti.reconnoiter.AMQListener;
import com.espertech.esper.client.*;

import org.apache.log4j.BasicConfigurator;

class IEPEngine {
  static public void main(String[] args) {
    BasicConfigurator.configure();

    Configuration config = new Configuration();
    config.addEventTypeAutoName("com.omniti.reconnoiter.event");
    EPServiceProvider epService = EPServiceProviderManager.getDefaultProvider(config);

    AMQListener l = new AMQListener(epService);

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
