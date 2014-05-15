/*
 * Copyright (c) 2014, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
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

import com.omniti.labs.FqClient;
import com.omniti.labs.FqClientImplNoop;
import com.omniti.labs.FqClientImplInterface;
import com.omniti.labs.FqCommand;
import com.omniti.labs.FqMessage;

public class FQBroker implements IMQBroker  {
  static Logger logger = Logger.getLogger(FQBroker.class.getName());

  private FqClient[] client;
  protected String userName;
  protected String password;
  protected String hostName[];
  protected int portNumber;
  protected String alertExchangeName;
  protected String alertRoutingKey;
  protected Integer heartBeat;
  protected IEventHandler eh;
  private FqNoit impl;

  private class FqNoit extends FqClientImplNoop {
    private FQBroker parent;
    private FqClient client;
    public FqNoit(FQBroker p) {
      super();
      parent = p;
    }
    public void setClient(FqClient c) throws InUseException {
      if(client != null) throw new InUseException();
      client = c;
    }
    public void commandError(Throwable e) {
      while(true) {
        try {
          client.creds(null, portNumber, userName, password);
          return;
        } catch(java.net.UnknownHostException uhe) { }
      }
    }
  }

  public FQBroker(StratconConfig config) {
    userName = config.getBrokerParameter("username", "guest");
    password = config.getBrokerParameter("password", "guest");
    hostName = config.getBrokerParameter("hostname", "127.0.0.1").split(",");
    portNumber = Integer.parseInt(config.getBrokerParameter("port", "8765"));
    heartBeat = Integer.parseInt(config.getBrokerParameter("heartbeat", "1000"));
    alertRoutingKey = config.getBrokerParameter("routingkey", "noit.alerts.");
    alertExchangeName = config.getBrokerParameter("exchange", "noit.alerts");

    client = new FqClient[hostName.length];
    for(int i=0;i<hostName.length; i++) {
      try {
        FqNoit impl = new FqNoit(this);
        client[i] = new FqClient(impl);
        client[i].creds(hostName[i], portNumber, userName, password);
        client[i].connect();
      } catch(Exception e) {
        throw new RuntimeException(e);
      }
    }
  }

  //
  public void disconnect() {
  }
  public void connect() throws Exception {
  }

  private String getAlertExchangeName() { return alertExchangeName; }
  private String getAlertRoutingKey() { return alertRoutingKey; }
  public void alert(String key, String json) {
    String routingKey;
    if(key == null) routingKey = getAlertRoutingKey();
    else routingKey = getAlertRoutingKey() + key;
    try {
      FqMessage msg = new FqMessage();
      msg.setRoute(routingKey.getBytes());
      msg.setExchange(getAlertExchangeName().getBytes());
      msg.setPayload(json.getBytes());
      for(int i=0;i<client.length;i++) client[i].send(msg);
    } catch (Exception e) {
      e.printStackTrace();
      try { connect(); }
      catch (Exception ignored) { }
    }
  }
}
