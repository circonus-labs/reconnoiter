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

package com.omniti.reconnoiter;

import java.lang.System;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.List;
import com.omniti.reconnoiter.MQListener;
import com.omniti.reconnoiter.broker.MQFactory;
import com.omniti.reconnoiter.broker.BrokerFactory;
import com.omniti.reconnoiter.StratconConfig;
import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.*;
import org.apache.log4j.BasicConfigurator;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import com.omniti.reconnoiter.broker.IMQMQ;
import com.omniti.reconnoiter.broker.IMQBroker;
import clojure.lang.RT;
import clojure.lang.Compiler;
import java.io.StringReader;

class IEPRiemann {
  private MQListener mql;

  public IEPRiemann(StratconConfig sconf) {
    try { RT.load("riemann.core"); }
    catch (Exception e) { throw new RuntimeException(e); }
    String nadef = "(ns reconnoiter " +
                   "  (:import (com.omniti.reconnoiter EventHandler))) " +
                   "(def alert " +
                   "  (clojure.core/fn [e] " +
                   "    (EventHandler/_sendAlert " +
                   "      (riemann.common/event-to-json e)))) " +
                   "(def alert-key " +
                   "  (clojure.core/fn [k] " +
                   "    (clojure.core/fn [e] " +
                   "      (EventHandler/_sendAlert " +
                   "        k (riemann.common/event-to-json e))))) " +
    "";
    Compiler.load(new StringReader(nadef));
    riemann.bin.main(new String[] {
      sconf.getIepParameter("riemann", "config") }
    );
    IMQMQ mq = MQFactory.getMQ(sconf);
    IMQBroker broker = BrokerFactory.getBroker(sconf);
    EventHandler eh = new EventHandler(mq, broker);
    mql = new MQListener(eh, mq);
  }

  public void start() {
    Thread listener = new Thread(mql);
    listener.start();
  }

  static public void main(String[] args) {
    BasicConfigurator.configure();
    if(args.length != 1) {
      System.err.println("Requires exactly one argument");
      return;
    }
    StratconConfig sconf = new StratconConfig(args[0]);
    (new IEPRiemann(sconf)).start();

    System.err.println("IEPRiemann ready...");
    BufferedReader stdin = new BufferedReader(new InputStreamReader(System.in));
    while(true) {
      try {
        if(stdin.readLine() == null) throw new Exception("all done");
      } catch(Exception e) {
        System.exit(-1);
      }
    }
  }
}
