/*
 * Copyright (c) 2013, Circonus, Inc. All rights reserved.
 * Copyright (c) 2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package com.omniti.reconnoiter.broker;

import java.lang.reflect.Constructor;

import javax.jms.Connection;
import javax.jms.Destination;
import javax.jms.MessageConsumer;
import javax.jms.MessageProducer;
import javax.jms.DeliveryMode;
import javax.jms.Session;
import javax.jms.Message;
import javax.jms.TextMessage;

import org.apache.activemq.broker.BrokerService;
import org.apache.activemq.ActiveMQConnectionFactory;

import com.omniti.reconnoiter.IEventHandler;
import com.omniti.reconnoiter.StratconConfig;

public class AMQMQ implements IMQMQ {
  private String hostName;
  private int portNumber;
  private Class listenerClass;
  private BrokerService amqBroker;

  @SuppressWarnings("unchecked")
  public AMQMQ(StratconConfig config) {
    this.hostName = config.getBrokerParameter("hostname", "127.0.0.1");
    this.portNumber = Integer.parseInt(config.getBrokerParameter("port", "61613"));
  }

  private MessageConsumer consumer;

  public void disconnect() {
  }
  public void connect() throws Exception {
    try {
      amqBroker = new BrokerService();
      amqBroker.setUseJmx(false);
      amqBroker.addConnector("vm://localhost");
      amqBroker.addConnector("stomp://" + hostName + ":" + portNumber);
      amqBroker.start();
    } catch(Exception e) {
      System.err.println("Cannot broker messages: " + e);
    }
    ActiveMQConnectionFactory connectionFactory=new ActiveMQConnectionFactory("vm://localhost");

    Connection connection=connectionFactory.createConnection();
    connection.start();
    Session session=connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

    Destination destination=session.createQueue("noit.firehose");
    consumer = session.createConsumer(destination);
  }
  
  public void consume(IEventHandler eh) {
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
        } catch(Exception ie) { ie.printStackTrace(); }
      }
    }
  }
}
