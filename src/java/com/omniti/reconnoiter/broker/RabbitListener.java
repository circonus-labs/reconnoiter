/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import org.apache.log4j.Logger;
import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.UpdateListener;
import com.espertech.esper.client.util.JSONEventRenderer;
import com.omniti.reconnoiter.event.StratconQuery;
import com.rabbitmq.client.Channel;
import com.rabbitmq.client.MessageProperties;

public class RabbitListener implements UpdateListener {
  static Logger logger = Logger.getLogger(RabbitListener.class.getName());
  protected EPServiceProvider epService;
  protected StratconQuery sq;
  protected String routingKey;
  protected String exchangeName;
  protected RabbitBroker broker;


  public RabbitListener(EPServiceProvider epService, StratconQuery sq,
                        RabbitBroker broker, String exchangeName, String routingKey) {
    try {
      this.epService = epService;
      this.sq = sq;
      EPStatement statement=sq.getStatement();
      this.routingKey = routingKey + sq.getName();
      this.exchangeName = exchangeName;
      this.broker = broker;
      
      // Create the connection and add an exchange
      boolean internal = false, durable = true, autoDelete = false;
      logger.debug("channel.exchangeDeclare -> " + exchangeName);
      broker.getChannel().exchangeDeclare(exchangeName, "topic", durable, autoDelete, internal, null);  
    } catch(Exception e) {
      e.printStackTrace();
    }
  }

  public void processEvent(EventBean event) {  
    JSONEventRenderer jsonRenderer = epService.getEPRuntime().
                                               getEventRenderer().
                                               getJSONRenderer(sq.getStatement().getEventType());

    String output = jsonRenderer.render(sq.getName(), event);
    try {
      byte[] messageBodyBytes = output.getBytes();
      broker.getChannel().basicPublish(exchangeName, routingKey, MessageProperties.TEXT_PLAIN, messageBodyBytes);
    } catch(Exception e) {
      System.err.println(e);
      try {
        broker.disconnect();
        broker.connect();
      } catch(Exception be) {
        System.err.println(be);
      }
    }
  }

  public void update(EventBean[] newEvents, EventBean[] oldEvents) {
    for(int i = 0; i < newEvents.length; i++) {
      processEvent(newEvents[i]);
    }
  }

}
