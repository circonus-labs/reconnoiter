/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import com.omniti.reconnoiter.broker.IMQBroker;
import com.omniti.reconnoiter.MessageHandler;
import com.omniti.reconnoiter.event.*;
import java.lang.Runnable;

import com.espertech.esper.client.EPServiceProvider;
import java.util.concurrent.ConcurrentHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;

public class MQListener implements Runnable {
    private EPServiceProvider epService;
    private ConcurrentHashMap<UUID,StratconQueryBase> queries;
    private IMQBroker broker;
    private LinkedList<StratconMessage> preproc;
    private LinkedList<StratconMessage> queries_toload;
    private LinkedList<MessageHandler>  alternates;
    private boolean booted = false;
    private EventHandler eh = null;

    public MQListener(EPServiceProvider epService, IMQBroker broker) {
      this.queries = new ConcurrentHashMap<UUID,StratconQueryBase>();
      this.epService = epService;
      this.broker = broker;
      preproc = new LinkedList<StratconMessage>();
      queries_toload = new LinkedList<StratconMessage>();
      alternates = new LinkedList<MessageHandler>();
    }

    public void addObserver(MessageHandler mh) {
      alternates.add(mh);
    }
    public void preprocess(StratconMessage m) throws Exception {
      if(booted) throw new Exception("Already booted");
      if(m instanceof StratconQuery)
        queries_toload.add(m);
      else
        preproc.add(m);
    }

    protected void process(EventHandler eh, List<StratconMessage> l) {
      for (StratconMessage m : l) {
        try { eh.processMessage(m); }
        catch (Exception e) {
          System.err.println("Something went wrong preprocessing events:");
          e.printStackTrace();
        }
      }
    }
    public void booted() {
      booted = true;
    }
    public EventHandler getEventHandler() { return eh; }
    public IMQBroker getBroker() { return broker; }
    public void run() {
      eh = new EventHandler(queries, this.epService, broker);
      for ( MessageHandler mh : alternates ) eh.addObserver(mh);
      process(eh, preproc);
      booted();
      while(true) {
        try {
          broker.connect();
          process(eh, queries_toload);
          try { broker.consume(eh); } catch (Exception anything) {}
          broker.disconnect();
        }
        catch (Exception e) {
          System.err.println("MQ connection failed: " + e.getMessage());
        }
        try { Thread.sleep(1000); } catch (InterruptedException ignore) {}
      }
    }
}
