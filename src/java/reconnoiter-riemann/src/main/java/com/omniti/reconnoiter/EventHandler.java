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

import org.apache.log4j.Logger;
import java.util.UUID;
import java.util.LinkedList;
import java.util.concurrent.ConcurrentHashMap;

import com.omniti.reconnoiter.broker.IMQMQ;
import com.omniti.reconnoiter.broker.IMQBroker;
import com.omniti.reconnoiter.event.*;
import com.omniti.reconnoiter.MessageHandler;
import com.omniti.reconnoiter.IEventHandler;
import java.util.Map;
import java.util.HashMap;
import java.util.concurrent.atomic.AtomicLong;

import riemann.codec.Event;
import java.io.StringReader;
import clojure.lang.RT;
import clojure.lang.Var;
import clojure.lang.Compiler;
import clojure.lang.Atom;
import clojure.lang.Symbol;
import clojure.lang.Namespace;
import clojure.lang.PersistentVector;

public class EventHandler implements IEventHandler {
  static Logger logger = Logger.getLogger(EventHandler.class.getName());
  private LinkedList<MessageHandler> alternates;
  private IMQMQ mq;
  private IMQBroker broker;
  private AtomicLong events_handled_num;
  private AtomicLong events_handled_microseconds;
  private Atom coreAtom;
  private java.lang.Object core;
  private Var stream;
  static private EventHandler _global;

  static public void _sendAlert(String key, String json) {
    _global.sendAlert(key,json);
  }
  static public void _sendAlert(String json) {
    _global.sendAlert(json);
  }
  static public void _coreReload() {
    _global.coreReload();
  }
  public EventHandler(IMQMQ mq, IMQBroker broker) {
    _global = this;
    this.mq = mq;
    this.broker = broker;
    alternates = new LinkedList<MessageHandler>();
    events_handled_num = new AtomicLong(0);
    events_handled_microseconds = new AtomicLong(0);

    coreAtom = (Atom)RT.var("riemann.config", "core").deref();
    String watcher =
      "(clojure.core/add-watch " +
      "  riemann.config/core 1 " +
      "    (fn [ref key old new] " +
      "        (com.omniti.reconnoiter.EventHandler/_coreReload)))";
    Compiler.load(new StringReader(watcher));
    stream = RT.var("riemann.core", "stream!");
    coreReload();
  }
  public void coreReload() {
    core = coreAtom.deref();
    logger.info("core changed, reloaded: " + core);
  }
  public void addObserver(MessageHandler mh) {
    alternates.add(mh);
  }
  public IMQMQ getMQ() { return mq; }
  public IMQBroker getBroker() { return broker; }

  public void processMessage(StratconMessage m) throws Exception {
    for ( MessageHandler mh : alternates )
      if(mh.observe(m, null) == true)
        return;
    long start = System.nanoTime();
    m.handle(this);
    long us = (System.nanoTime() - start) / 1000;
    events_handled_num.incrementAndGet();
    events_handled_microseconds.addAndGet(us);
  }
  public long getNumEventsHandled() { return events_handled_num.longValue(); }
  public long getMicrosecondsHandlingEvents() { return events_handled_microseconds.longValue(); }
  public void processMessage(StratconMessage[] messages) throws Exception {
    Exception last = null;
    for ( StratconMessage m : messages ) {
      if(m != null) try { processMessage(m); } catch (Exception e) { last = e; }
    }
    if(last != null) throw(last);
  }
  public void processMessage(String payload) throws Exception {
    Exception last = null;
    StratconMessage[] messages = StratconMessage.makeMessages(payload);
    if(messages == null) {
      System.err.println("Can't grok:\n" + payload);
    }
    processMessage(messages);
  }
  public boolean stopProcessing(StratconMessage m, String source) {
    for ( MessageHandler mh : alternates )
      if(mh.observe(m, source) == true)
        return true;
    return false;
  }
  public void sendAlert(String json) {
    broker.alert(null, json);
  }
  public void sendAlert(String name, String json) {
    broker.alert(name, json);
  }
  public void sendEvent(StratconMessage m) {
    Event e = null;
    NoitMetricNumeric nmn = null;
    NoitMetricText nmt = null;
    if (m instanceof NoitMetric) {
      NoitMetric nm = (NoitMetric)m;
      if(nm.isNumeric()) nmn = nm.getNumeric();
      if(nm.isText()) nmt = nm.getText();
    }
    else if (m instanceof NoitMetricNumeric) {
      nmn = (NoitMetricNumeric)m;
    }
    else if (m instanceof NoitMetricText) {
      nmt = (NoitMetricText)m;
    }
    if(nmn != null) {
      PersistentVector tags = PersistentVector.create(new java.lang.String[] {
                      "reconnoiter",
                      "check:" + nmn.getUuid(),
                      "name:" + nmn.getCheck_name(),
                      "module:" + nmn.getCheck_module(),
                      "noit:" + nmn.getNoit() });
      e = new Event(nmn.getCheck_target(), // host
                    nmn.getName(), // service
                    null, // state
                    null, // description
                    nmn.getValue(), // value
                    tags, // tags
                    nmn.getTime()/1000, //time
                    10); //ttl
    }
    else if(nmt != null) {
      PersistentVector tags = PersistentVector.create(new java.lang.String[] {
                      "reconnoiter",
                      "check:" + nmt.getUuid(),
                      "name:" + nmt.getCheck_name(),
                      "module:" + nmt.getCheck_module(),
                      "noit:" + nmt.getNoit() });
      e = new Event(nmt.getCheck_target(), // host
                    nmt.getName(), // service
                    null, // state
                    nmt.getMessage(), // description
                    null, // value
                    tags, // tags
                    nmt.getTime()/1000, //time
                    10); //ttl
    }
    if(e != null) {
      stream.invoke(core, e);
    }
  }
}
