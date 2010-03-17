/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import java.lang.reflect.Constructor;

import javax.jms.Connection;
import javax.jms.Destination;
import javax.jms.MessageConsumer;
import javax.jms.Session;
import javax.jms.Message;
import javax.jms.TextMessage;

import org.apache.activemq.ActiveMQConnectionFactory;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconConfig;
import com.omniti.reconnoiter.event.StratconQuery;

public class AMQBroker implements IMQBroker {
  private String hostName;
  private int portNumber;
  private Class listenerClass;
  private Constructor<UpdateListener> con;

  @SuppressWarnings("unchecked")
  public AMQBroker(StratconConfig config) {
    this.hostName = config.getBrokerParameter("hostname", "127.0.0.1");
    this.portNumber = Integer.parseInt(config.getBrokerParameter("port", "61613"));
    String className = config.getBrokerParameter("listenerClass", "com.omniti.reconnoiter.broker.AMQListener");
    try {
      this.listenerClass = Class.forName(className);
      this.con = this.listenerClass.getDeclaredConstructor(
          new Class[] { EPServiceProvider.class, StratconQuery.class, String.class }
      );
    }
    catch(java.lang.ClassNotFoundException e) {
      throw new RuntimeException("Cannot find class: " + className);
    }
    catch(java.lang.NoSuchMethodException e) {
      throw new RuntimeException("Cannot find constructor for class: " + className);
    }

  }

  private MessageConsumer consumer;

  public void disconnect() {
  }
  public void connect() throws Exception {
    BrokerFactory.getAMQBrokerService("stomp://" + hostName + ":" + portNumber);
    ActiveMQConnectionFactory connectionFactory=new ActiveMQConnectionFactory("vm://localhost");

    Connection connection=connectionFactory.createConnection();
    connection.start();
    Session session=connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
    Destination destination=session.createQueue("noit.firehose");

    consumer = session.createConsumer(destination);
  }
  
  public void consume(EventHandler eh) {
    while (true) {
      Message message = null;
      try {
        message = consumer.receive(1000);
      } catch(Exception ignored) {
      }
      if (message != null && message instanceof TextMessage) {
        TextMessage textMessage = (TextMessage) message;
        try {
          String xml = textMessage.getText();
          eh.processMessage(xml);
        } catch(Exception ie) {
          System.err.println(ie);
        }
      }
    }
  }
  
  public UpdateListener getListener(EPServiceProvider epService, StratconQuery sq) {
    UpdateListener l = null;
    try {
      l = con.newInstance(epService, sq, "vm://localhost");
    }
    catch(java.lang.InstantiationException ie) { }
    catch(java.lang.IllegalAccessException ie) { }
    catch(java.lang.reflect.InvocationTargetException ie) { }
    return l;
  }
}
