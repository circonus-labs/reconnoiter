package com.omniti.reconnoiter.broker;

import java.io.IOException;

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
  private String queueName;
  private String routingKey;
  private String alertQueue;
  private String alertExchangeName;

  public RabbitBroker(StratconConfig config) {
    this.userName = config.getBrokerParameter("username", "guest");
    this.password = config.getBrokerParameter("password", "guest");
    this.virtualHost = config.getBrokerParameter("virtualhost", "/");
    this.hostName = config.getBrokerParameter("hostname", "127.0.0.1");
    this.portNumber = Integer.parseInt(config.getBrokerParameter("port", "5672"));
    
    // This is a fanout exchange
    this.exchangeName = config.getMQParameter("exchange", "noit.firehose");
    // This queue is bound to the fanout exchange
    this.queueName = config.getMQParameter("queue", "noit.firehose");
    // No need for a routing key on a FO exchange
    this.routingKey = "";
  
    this.alertQueue = config.getBrokerParameter("queue", "noit.alerts.");
    this.alertExchangeName = config.getBrokerParameter("exchange", "noit.alerts");
  }
  
  // 
  public void connect() {
    ConnectionParameters params = new ConnectionParameters();
    params.setUsername(userName);
    params.setPassword(password);
    params.setVirtualHost(virtualHost);
    params.setRequestedHeartbeat(0);
    ConnectionFactory factory = new ConnectionFactory(params);
    try {
      Connection conn = factory.newConnection(hostName, portNumber);
      channel = conn.createChannel();
      
      channel.exchangeDeclare(exchangeName, "fanout");
      channel.queueDeclare(queueName);
      channel.queueBind(queueName, exchangeName, routingKey);

    } catch(Exception e) {
      System.err.println("Cannot broker messages");
    }
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
    return new RabbitListener(epService, sq, channel, alertExchangeName, alertQueue);
  }
}
