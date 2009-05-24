/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import com.omniti.reconnoiter.AMQBrokerSingleton;
import com.omniti.reconnoiter.event.StratconQuery;
import java.lang.System;
import org.apache.activemq.ActiveMQConnectionFactory;
import javax.jms.Connection;
import javax.jms.Session;
import javax.jms.Destination;
import javax.jms.MessageProducer;
import javax.jms.DeliveryMode;
import javax.jms.TextMessage;
import javax.jms.JMSException;
import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.util.JSONEventRenderer;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.UpdateListener;

public class AMQOutput implements UpdateListener {
    private EPServiceProvider epService;
    private EPStatement statement;
    private ActiveMQConnectionFactory connectionFactory;
    private Connection connection;
    private Session session;
    private Destination destination;
    private MessageProducer producer;
    private StratconQuery sq;

    public AMQOutput(EPServiceProvider epService, StratconQuery sq) {
      try {
        // we just need it started up
        AMQBrokerSingleton.getBroker();
        this.epService = epService;
        this.sq = sq;
        this.statement = sq.getStatement();

        connectionFactory = new ActiveMQConnectionFactory("tcp://localhost:61616");
        connection = connectionFactory.createConnection();
        connection.start();
        session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
        destination = session.createTopic("noit.alerts." + sq.getName());

        producer = session.createProducer(destination);
        producer.setDeliveryMode(DeliveryMode.PERSISTENT);
      } catch(Exception e) {
        System.err.println("Cannot broker messages");
      }
    }
    public void update(EventBean[] newEvents, EventBean[] oldEvents) {
      System.err.println("AMQOutput -> dispatch");
      for(int i = 0; i < newEvents.length; i++) {
        EventBean event = newEvents[i];

        JSONEventRenderer jsonRenderer = epService.getEPRuntime().
                                                   getEventRenderer().
                                                   getJSONRenderer(sq.getStatement().getEventType());
        String output = jsonRenderer.render(sq.getName(), event);
        try {
          TextMessage message = session.createTextMessage(output);
          producer.send(message);
        }  catch(JMSException e) {
          System.err.println(e);
        }
        System.err.println(output);
      }
    }
}
