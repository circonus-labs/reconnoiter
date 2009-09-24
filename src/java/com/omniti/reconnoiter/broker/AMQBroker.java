package com.omniti.reconnoiter.broker;

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

  public AMQBroker(StratconConfig config) {
  }

  private MessageConsumer consumer;

  public void connect() {
    BrokerFactory.getAMQBrokerService();
    ActiveMQConnectionFactory connectionFactory=new ActiveMQConnectionFactory("tcp://localhost:61616");
    try {
      Connection connection=connectionFactory.createConnection();
      connection.start();
      Session session=connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
      Destination destination=session.createQueue("noit.firehose");
  
      consumer = session.createConsumer(destination);
    } catch(Exception e) {
      System.err.println("Cannot broker messages");
    }
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
    return new AMQListener(epService, sq);
  }
}
