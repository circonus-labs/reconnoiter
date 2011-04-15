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

import org.apache.log4j.Logger;
import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.UpdateListener;
import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconConfig;
import com.omniti.reconnoiter.event.StratconQuery;
import com.rabbitmq.client.Connection;
import com.rabbitmq.client.Channel;
import com.rabbitmq.client.ConnectionFactory;
import com.rabbitmq.client.QueueingConsumer;


public class RabbitBroker implements IMQBroker  {
  static Logger logger = Logger.getLogger(RabbitBroker.class.getName());
  private int cidx;
  private Connection conn;
  private Channel channel;
  private boolean noAck = false;
  private String userName;
  private String password;
  private String virtualHost;
  private String hostName[];
  private ConnectionFactory factory[];
  private int portNumber;
  private String exchangeName;
  private String exchangeType;
  private String declaredQueueName;
  private String queueName;
  private String routingKey;
  private String alertRoutingKey;
  private String alertExchangeName;
  private Integer heartBeat;
  private Integer connectTimeout;
  private Class listenerClass;
  private Constructor<UpdateListener> con;

  @SuppressWarnings("unchecked") 
  public RabbitBroker(StratconConfig config) {
    this.cidx = 0;
    this.userName = config.getBrokerParameter("username", "guest");
    this.password = config.getBrokerParameter("password", "guest");
    this.virtualHost = config.getBrokerParameter("virtualhost", "/");
    this.hostName = config.getBrokerParameter("hostname", "127.0.0.1").split(",");
    this.portNumber = Integer.parseInt(config.getBrokerParameter("port", "5672"));
    this.heartBeat = Integer.parseInt(config.getBrokerParameter("heartbeat", "5000"));
    this.heartBeat = (this.heartBeat + 999) / 1000; // (ms -> seconds, rounding up)
    this.connectTimeout = Integer.parseInt(config.getBrokerParameter("connect_timeout", "5000"));
    
    String className = config.getBrokerParameter("listenerClass", "com.omniti.reconnoiter.broker.RabbitListener");
    try {
      this.listenerClass = Class.forName(className);
      this.con = this.listenerClass.getDeclaredConstructor(
          new Class[] { EPServiceProvider.class, StratconQuery.class, RabbitBroker.class,
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
    this.declaredQueueName = config.getMQParameter("queue", "");
    this.routingKey = config.getMQParameter("routingkey", "");
  
    this.alertRoutingKey = config.getBrokerParameter("routingkey", "noit.alerts.");
    this.alertExchangeName = config.getBrokerParameter("exchange", "noit.alerts");

    this.factory = new ConnectionFactory[this.hostName.length];
    for(int i = 0; i<hostName.length; i++) {
      this.factory[i] = new ConnectionFactory();
      this.factory[i].setUsername(this.userName);
      this.factory[i].setPassword(this.password);
      this.factory[i].setVirtualHost(this.virtualHost);
      this.factory[i].setRequestedHeartbeat(this.heartBeat);
      this.factory[i].setConnectionTimeout(this.connectTimeout);
      this.factory[i].setPort(this.portNumber);
      this.factory[i].setHost(this.hostName[i]);
    }
  }
  
  // 
  public void disconnect() {
    logger.info("AMQP disconnect.");
    try {
      channel.abort();
      channel = null;
    }
    catch (Exception e) { }
    try {
      conn.abort();
      conn = null;
    }
    catch (Exception e) { }
  }
  public void connect() throws Exception {
    int offset = ++cidx % factory.length;
    logger.info("AMQP connect to " + this.hostName[offset]);
    conn = factory[offset].newConnection();

    if(conn == null) throw new Exception("connection failed");

    channel = conn.createChannel();
    boolean passive = false, exclusive = false, durable = false, autoDelete = false;
    channel.exchangeDeclare(exchangeName, exchangeType, passive, durable, autoDelete, null);
    exclusive = true; autoDelete = true;
    queueName = channel.queueDeclare(declaredQueueName, durable, exclusive, autoDelete, null).getQueue();
    channel.queueBind(queueName, exchangeName, routingKey);
    if(!routingKey.equals(""))
      channel.queueBind(queueName, exchangeName, "");
  }
  public Channel getChannel() { return channel; }
  
  public void consume(EventHandler eh) throws IOException {
    QueueingConsumer consumer = new QueueingConsumer(channel);

    channel.basicConsume(queueName, noAck, consumer);
    
    while (true) {
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
  }

  public UpdateListener getListener(EPServiceProvider epService, StratconQuery sq) {
    UpdateListener l = null;
    try {
      l = con.newInstance(epService, sq, this, alertExchangeName, alertRoutingKey);
    }
    catch(java.lang.InstantiationException ie) { }
    catch(java.lang.IllegalAccessException ie) { }
    catch(java.lang.reflect.InvocationTargetException ie) { }
    return l;
  }
}
