package com.omniti.reconnoiter;

import com.omniti.reconnoiter.AMQBrokerSingleton;
import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.StratconQuery;
import com.omniti.reconnoiter.event.StratconQueryStop;
import com.omniti.reconnoiter.event.NoitEvent;
import java.lang.System;
import java.lang.Runnable;
import org.apache.activemq.ActiveMQConnectionFactory;
import javax.jms.Connection;
import javax.jms.Session;
import javax.jms.Destination;
import javax.jms.MessageConsumer;
import javax.jms.DeliveryMode;
import javax.jms.Message;
import javax.jms.TextMessage;
import javax.jms.JMSException;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.UpdateListener;
import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.EPStatement;
import java.util.concurrent.ConcurrentHashMap;
import java.util.UUID;

public class AMQListener implements Runnable {
    private EPServiceProvider epService;
    private ActiveMQConnectionFactory connectionFactory;
    private Connection connection;
    private Session session;
    private Destination destination;
    private MessageConsumer consumer;
    private ConcurrentHashMap<UUID,StratconQuery> queries;

    public AMQListener(EPServiceProvider epService) {
      this.queries = new ConcurrentHashMap<UUID,StratconQuery>();
      this.epService = epService;
      try {
        // we just need it started up
        AMQBrokerSingleton.getBroker();

        connectionFactory = new ActiveMQConnectionFactory("tcp://localhost:61616");
        connection = connectionFactory.createConnection();
        connection.start();
        session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
        destination = session.createQueue("noit.firehose");

        consumer = session.createConsumer(destination);
      } catch(Exception e) {
        System.err.println("Cannot broker messages");
      }
    }
    public void run() {
      while(true) {
        Message message = null;
        try {
          message = consumer.receive(1000);
        } catch(Exception e) {
          
        }
        if (message != null && message instanceof TextMessage) {
          TextMessage textMessage = (TextMessage) message;
          try {
            String xml = textMessage.getText();
            StratconMessage m = StratconMessage.makeMessage(xml);
            if(m == null) {
              System.err.println("Can't grok:\n" + xml);
            }
            if(m instanceof StratconQuery) {
              StratconQuery sq = (StratconQuery) m;

              if(queries.containsKey(sq.getUUID())) throw (new Exception("Duplicate Query"));

              EPStatement statement = epService.getEPAdministrator().createEPL(sq.getExpression());
              AMQOutput o = new AMQOutput(epService, statement, sq.getName());

              statement.addListener(o);
              sq.setStatement(statement);
              sq.setListener(o);
              queries.put(sq.getUUID(), sq);
              System.err.println("Creating Query: " + sq.getUUID());
            }
            else if(m instanceof StratconQueryStop) {
              StratconQuery sq = queries.get(((StratconQueryStop) m).getUUID());
              if(sq != null) {
                queries.remove(sq.getUUID());
                sq.destroy();
              }
            }
            else if(m instanceof NoitEvent) {
              epService.getEPRuntime().sendEvent(((NoitEvent) m).getDocument());
            }
          } catch(Exception ie) {
            System.err.println(ie);
          }
        }
      }
    }
}
