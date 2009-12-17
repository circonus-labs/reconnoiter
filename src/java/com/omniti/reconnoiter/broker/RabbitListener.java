/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.UpdateListener;
import com.espertech.esper.client.util.JSONEventRenderer;
import com.omniti.reconnoiter.event.StratconQuery;
import com.rabbitmq.client.Channel;
import com.rabbitmq.client.MessageProperties;

public class RabbitListener implements UpdateListener {

  
  private EPServiceProvider epService;
  private StratconQuery sq;
  private String routingKey;
  private String exchangeName;
  private Channel channel;


  public RabbitListener(EPServiceProvider epService, StratconQuery sq, Channel channel, String exchangeName, String routingKey) {
    try {
      this.epService = epService;
      this.sq = sq;
      EPStatement statement=sq.getStatement();
      this.routingKey = routingKey + sq.getName();
      this.exchangeName = exchangeName;
      this.channel = channel;
      
      // Create the connection and add an exchange
      channel.exchangeDeclare(exchangeName, "topic", false, false, false, null);  
    } catch(Exception e) {
      e.printStackTrace();
    }
  }
  
  
  public void update(EventBean[] newEvents, EventBean[] oldEvents) {
    System.err.println("RMQOutput -> dispatch");
    for(int i = 0; i < newEvents.length; i++) {
      EventBean event = newEvents[i];

      JSONEventRenderer jsonRenderer = epService.getEPRuntime().
                                                 getEventRenderer().
                                                 getJSONRenderer(sq.getStatement().getEventType());
      String output = jsonRenderer.render(sq.getName(), event);
      try {
        byte[] messageBodyBytes = output.getBytes();
        channel.basicPublish(exchangeName, routingKey, MessageProperties.TEXT_PLAIN, messageBodyBytes);
      }  catch(Exception e) {
        System.err.println(e);
      }
      System.err.println(output);
    }
  }

}
