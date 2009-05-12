package com.omniti.reconnoiter;

import com.omniti.reconnoiter.AMQBrokerSingleton;
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

    public AMQOutput(EPServiceProvider epService, EPStatement statement, String name) {
      try {
        // we just need it started up
        AMQBrokerSingleton.getBroker();
        this.epService = epService;
        this.statement = statement;

        connectionFactory = new ActiveMQConnectionFactory("tcp://localhost:61616");
        connection = connectionFactory.createConnection();
        connection.start();
        session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);
        destination = session.createTopic("noit.alerts." + name);

        producer = session.createProducer(destination);
        producer.setDeliveryMode(DeliveryMode.PERSISTENT);
      } catch(Exception e) {
        System.err.println("Cannot broker messages");
      }
    }
    public void update(EventBean[] newEvents, EventBean[] oldEvents) {
      EventBean event = newEvents[0];

      JSONEventRenderer jsonRenderer = epService.getEPRuntime().
                                                 getEventRenderer().
                                                 getJSONRenderer(statement.getEventType());
      String output = jsonRenderer.render("MyEvent", event);
      try {
        TextMessage message = session.createTextMessage(output);
        producer.send(message);
      }  catch(JMSException e) {
        System.err.println(e);
      }
      System.err.println(output);
    }
}
