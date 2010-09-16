/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import java.io.IOException;
import java.lang.reflect.Constructor;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconConfig;
import com.omniti.reconnoiter.event.StratconQuery;
import com.rabbitmq.client.Connection;
import com.rabbitmq.client.Channel;
import com.rabbitmq.client.ConnectionFactory;
import com.rabbitmq.client.ConnectionParameters;
import com.rabbitmq.client.QueueingConsumer;


public class RabbitBroker implements IMQBroker  {

  private Channel channel;
  private boolean noAck = false;
  private String userName;
  private String password;
  private String virtualHost;
  private String hostName;
  private int portNumber;
  private String exchangeName;
  private String exchangeType;
  private String queueName;
  private String routingKey;
  private String alertRoutingKey;
  private String alertExchangeName;
  private Class listenerClass;
  private Constructor<UpdateListener> con;

  @SuppressWarnings("unchecked") 
  public RabbitBroker(StratconConfig config) {
    this.userName = config.getBrokerParameter("username", "guest");
    this.password = config.getBrokerParameter("password", "guest");
    this.virtualHost = config.getBrokerParameter("virtualhost", "/");
    this.hostName = config.getBrokerParameter("hostname", "127.0.0.1");
    this.portNumber = Integer.parseInt(config.getBrokerParameter("port", "5672"));
    
    String className = config.getBrokerParameter("listenerClass", "com.omniti.reconnoiter.broker.RabbitListener");
    try {
      this.listenerClass = Class.forName(className);
      this.con = this.listenerClass.getDeclaredConstructor(
          new Class[] { EPServiceProvider.class, StratconQuery.class, Channel.class,
                        String.class, String.class }
      );
    }
    catch(java.lang.ClassNotFoundException e) {
      throw new RuntimeException("Cannot find class: " + className);
    }
    catch(java.lang.NoSuchMethodException e) {
      throw new RuntimeException("Cannot find constructor for class: " + className);
    }

    this.exchangeType = config.getMQParameter("exchangetype", "fanout");
    this.exchangeName = config.getMQParameter("exchange", "noit.firehose");
    this.queueName = config.getMQParameter("queue", "");
    this.routingKey = config.getMQParameter("routingkey", "");
  
    this.alertRoutingKey = config.getBrokerParameter("routingkey", "noit.alerts.");
    this.alertExchangeName = config.getBrokerParameter("exchange", "noit.alerts");
  }
  
  // 
  public void disconnect() {
    try {
      channel.getConnection().abort();
      channel.abort();
    }
    catch (Exception e) { }
  }
  public void connect() throws Exception {
    ConnectionParameters params = new ConnectionParameters();
    params.setUsername(userName);
    params.setPassword(password);
    params.setVirtualHost(virtualHost);
    params.setRequestedHeartbeat(0);
    ConnectionFactory factory = new ConnectionFactory(params);
    Connection conn = factory.newConnection(hostName, portNumber);

    if(conn == null) throw new Exception("connection failed");

    channel = conn.createChannel();
    boolean passive = false, exclusive = false, durable = false, autoDelete = false;
    channel.exchangeDeclare(exchangeName, exchangeType, passive, durable, autoDelete, null);
    exclusive = true; autoDelete = true;
    queueName = channel.queueDeclare(queueName, passive, durable, exclusive, autoDelete, null).getQueue();
    channel.queueBind(queueName, exchangeName, routingKey);
    if(!routingKey.equals(""))
      channel.queueBind(queueName, exchangeName, "");
  }
  
  public void consume(EventHandler eh) {
    QueueingConsumer consumer = new QueueingConsumer(channel);

    try {
      channel.basicConsume(queueName, noAck, consumer);
    } catch (IOException e) {
      // TODO Not sure what to do here
      e.printStackTrace();
    }
    
    while (true) {
      try
      {
        QueueingConsumer.Delivery delivery;
        try {
          delivery = consumer.nextDelivery();
        } catch (InterruptedException ie) {
          continue;
        }
        // (process the message components ...)
      
        String xml = new String(delivery.getBody());
        try {
          eh.processMessage(xml);
        } catch (Exception e) {
          // TODO Auto-generated catch block
          e.printStackTrace();
        }
        channel.basicAck(delivery.getEnvelope().getDeliveryTag(), false);
      }
      catch (IOException ie) {
        ie.printStackTrace();
      }
    }
  }

  public UpdateListener getListener(EPServiceProvider epService, StratconQuery sq) {
    UpdateListener l = null;
    try {
      l = con.newInstance(epService, sq, channel, alertExchangeName, alertRoutingKey);
    }
    catch(java.lang.InstantiationException ie) { }
    catch(java.lang.IllegalAccessException ie) { }
    catch(java.lang.reflect.InvocationTargetException ie) { }
    return l;
  }
}
