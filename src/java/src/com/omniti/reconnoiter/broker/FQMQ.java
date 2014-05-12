/*
 * Copyright (c) 2014, Circonus, Inc. All rights reserved.
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
import com.omniti.labs.FqClient;
import com.omniti.labs.FqClientImplDebug;
import com.omniti.labs.FqClientImplNoop;
import com.omniti.labs.FqClientImplInterface;
import com.omniti.labs.FqCommand;
import com.omniti.labs.FqMessage;

public class FQMQ implements IMQMQ  {
  static Logger logger = Logger.getLogger(FQMQ.class.getName());
  private FqClient[] client;
  protected int idx = 0;
  protected String userName;
  protected String password;
  protected String hostName[];
  protected int portNumber;
  protected String exchangeName;
  protected String routingKey;
  protected Integer heartBeat;
  protected IEventHandler eh;
  private FqNoit impl;

  private class FqNoit extends FqClientImplDebug {
    private FQMQ parent;
    private FqClient client;
    public FqNoit(FQMQ p) {
      super();
      parent = p;
    }
    public void setClient(FqClient c) throws InUseException {
      if(client != null) throw new InUseException();
      client = c;
    }
    public void dispatch(FqMessage m) {
      try {
        if(parent.eh != null) parent.eh.processMessage(new String(m.getPayload()));
      } catch (Exception e) { e.printStackTrace(); }
    }
    public void commandError(Throwable e) {
      e.printStackTrace();
      while(true) {
        try {
          client.creds(null, portNumber, userName, password);
          return;
        } catch(java.net.UnknownHostException uhe) { }
      }
    }
    public void dispatchBindRequest(FqCommand.BindRequest cmd) {
      System.err.println("bind -> " + cmd.getBinding());
    }
    public void dispatchAuth(FqCommand.Auth a) {
      if(a.success()) {
        for (String rk : routingKey.split(",")) {
            if ( rk.equalsIgnoreCase("null") ) rk = "";
            FqCommand.BindRequest breq = new FqCommand.BindRequest(
              exchangeName, "prefix:\"" + rk + "\"", false
            );
            client.send(breq);
        }
      }
    }
  }
  public FQMQ(StratconConfig config) {
    userName = config.getMQTypeParameter("fq", "username", "guest");
    password = config.getMQTypeParameter("fq", "password", "guest");
    hostName = config.getMQTypeParameter("fq", "hostname", "127.0.0.1").split(",");
    portNumber = Integer.parseInt(config.getMQTypeParameter("fq", "port", "8765"));
    heartBeat = Integer.parseInt(config.getMQTypeParameter("fq", "heartbeat", "1000"));
    routingKey = config.getMQTypeParameter("fq", "routingkey", "");
    exchangeName = config.getMQTypeParameter("fq", "exchange", "");

    client = new FqClient[hostName.length];
    for(int i=0;i<hostName.length;i++) {
      try {
        FqNoit impl = new FqNoit(this);
        client[i] = new FqClient(impl);
        client[i].creds(hostName[i], portNumber, userName, password);
        client[i].setHeartbeat(heartBeat);
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
  
  public void consume(IEventHandler _eh) throws IOException {
    eh = _eh;
    while(true) {
      try { this.wait(); } catch (InterruptedException ie) {}
    }
  }
}
