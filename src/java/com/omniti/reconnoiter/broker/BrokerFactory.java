/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import org.apache.activemq.broker.BrokerService;

import com.omniti.reconnoiter.StratconConfig;

public class BrokerFactory {
  private static BrokerService amqBroker = null;
  
  public static IMQBroker getBroker(StratconConfig config) {
    // TODO if the broker is null default the AMQAdapter
    String broker = config.getBroker();
    
    if (broker == null)
      return new AMQBroker(config);
    
    if (broker.compareToIgnoreCase("rabbitmq") == 0) {
      return new RabbitBroker(config);
    }
    else if (broker.compareToIgnoreCase("activemq") == 0) {
      return new AMQBroker(config);
    }
    return new AMQBroker(config);
  }
  

  public static BrokerService getAMQBrokerService() {
    if(amqBroker == null) System.err.println("Cannot broker messages");
    return amqBroker;
  }
  public static BrokerService getAMQBrokerService(String stomp) {
    if(amqBroker != null) return amqBroker;
    try {
      amqBroker = new BrokerService();
      amqBroker.setUseJmx(false);
      amqBroker.addConnector("vm://localhost");
      amqBroker.addConnector(stomp);
      amqBroker.start();
    } catch(Exception e) {
      System.err.println("Cannot broker messages: " + e);
    }
    return amqBroker;
  }

}
