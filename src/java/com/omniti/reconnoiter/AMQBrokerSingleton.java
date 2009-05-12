package com.omniti.reconnoiter;

import java.lang.System;
import org.apache.activemq.broker.BrokerService;

public class AMQBrokerSingleton {
    private static BrokerService broker = null;

    public AMQBrokerSingleton() {
    }

    public static BrokerService getBroker() {
      if(broker != null) return broker;
      try {
        broker = new BrokerService();
        broker.setUseJmx(false);
        broker.addConnector("tcp://localhost:61616");
        broker.addConnector("stomp://localhost:61613");
        broker.start();
      } catch(Exception e) {
        System.err.println("Cannot broker messages: " + e);
      }
      return broker;
    }
}
