/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import java.io.IOException;

import org.apache.log4j.Logger;
import com.omniti.reconnoiter.IEventHandler;
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
  private boolean noAck = true;
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
  private String returnedQueueName;
  private String routingKey;
  private String alertRoutingKey;
  private String alertExchangeName;
  private Integer heartBeat;
  private Integer connectTimeout;
  private Class listenerClass;
  private boolean exclusiveQueue;
  private boolean durableQueue;
  private boolean durableExchange;

  public RabbitBroker(StratconConfig config) {
    this.conn = null;
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
    }
    catch(java.lang.NoClassDefFoundError e) { }
    catch(java.lang.ClassNotFoundException e) {
      logger.warn("Class " + className + " not found.");
    }

    this.exchangeType = config.getMQParameter("exchangetype", "fanout");
    this.durableExchange = config.getMQParameter("durableexchange", "false").equals("true");
    this.exchangeName = config.getMQParameter("exchange", "noit.firehose");
    this.exclusiveQueue = config.getMQParameter("exclusivequeue", "false").equals("true");
    this.durableQueue = config.getMQParameter("durablequeue", "false").equals("true");
    this.declaredQueueName = config.getMQParameter("queue", "reconnoiter-{node}-{pid}");
    this.routingKey = config.getMQParameter("routingkey", "");
  
    try {
      this.queueName = this.declaredQueueName;
      this.queueName = this.queueName.replace("{node}",
        java.net.InetAddress.getLocalHost().getHostName());
      this.queueName = this.queueName.replace("{pid}",
        java.lang.management.ManagementFactory.getRuntimeMXBean()
                                              .getName().replaceAll("@.+$", ""));
    } catch(java.net.UnknownHostException uhe) {
      System.err.println("Cannot self identify for queuename!");
      System.exit(-1);
    }     

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
    }
    catch (Exception e) { }
    channel = null;
    try {
      conn.abort();
    }
    catch (Exception e) { }
    conn = null;
  }
  public void connect() throws Exception {
    if(conn != null) disconnect();

    int offset = ++cidx % factory.length;
    logger.info("AMQP connect to " + this.hostName[offset]);
    conn = factory[offset].newConnection();

    if(conn == null) throw new Exception("connection failed");

    channel = conn.createChannel();
    boolean exclusive = false, internal = false, autoDelete = false;
    channel.exchangeDeclare(exchangeName, exchangeType,
                            durableExchange, autoDelete, internal, null);
    autoDelete = true;

    returnedQueueName = channel.queueDeclare(queueName, durableQueue,
                                             exclusiveQueue, autoDelete, null).getQueue();
    for (String rk : routingKey.split(",")) {
        if ( rk.equalsIgnoreCase("null") ) rk = "";
        channel.queueBind(returnedQueueName, exchangeName, rk);
    }
  }
  public Channel getChannel() { return channel; }
  
  public void consume(IEventHandler eh) throws IOException {
    QueueingConsumer consumer = new QueueingConsumer(channel);

    channel.basicConsume(returnedQueueName, noAck, consumer);
    
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
      if(!noAck)
        channel.basicAck(delivery.getEnvelope().getDeliveryTag(), false);
    }
  }

  public Class getListenerClass() { return listenerClass; }
  public String getAlertExchangeName() { return alertExchangeName; }
  public String getAlertRoutingKey() { return alertRoutingKey; }
}
