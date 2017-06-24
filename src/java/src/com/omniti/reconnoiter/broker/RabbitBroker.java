/*
 * Copyright (c) 2013-2017, Circonus, Inc. All rights reserved.
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

import java.io.IOException;

import org.apache.log4j.Logger;
import com.omniti.reconnoiter.IEventHandler;
import com.omniti.reconnoiter.StratconConfig;
import com.rabbitmq.client.Connection;
import com.rabbitmq.client.Channel;
import com.rabbitmq.client.MessageProperties;
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
  private String alertRoutingKey;
  private String alertExchangeName;
  private Integer heartBeat;
  private Integer connectTimeout;
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
    try { connect(); }
    catch (Exception e) { }
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

    boolean internal = false, durable = true, autoDelete = false;
    channel.exchangeDeclare(getAlertExchangeName(), "topic", durable, autoDelete, internal, null); 
  }
  
  private String getAlertExchangeName() { return alertExchangeName; }
  private String getAlertRoutingKey() { return alertRoutingKey; }
  public void alert(String key, String json) {
    String routingKey;
    if(key == null) routingKey = getAlertRoutingKey();
    else routingKey = getAlertRoutingKey() + key;
    try {
      byte[] messageBodyBytes = json.getBytes();
      channel.basicPublish(getAlertExchangeName(), routingKey,
                           MessageProperties.TEXT_PLAIN, messageBodyBytes);
    } catch (Exception e) {
      e.printStackTrace();
      try { connect(); }
      catch (Exception ignored) { }
    }
  }
}
