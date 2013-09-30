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

import com.omniti.reconnoiter.broker.IMQMQ;
import com.omniti.reconnoiter.MessageHandler;
import com.omniti.reconnoiter.event.*;
import java.lang.Runnable;

import java.util.concurrent.ConcurrentHashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;

public class MQListener implements Runnable {
    private IMQMQ mq;
    private LinkedList<StratconMessage> preproc;
    private LinkedList<MessageHandler>  alternates;
    private boolean booted = false;
    private IEventHandler eh = null;

    public MQListener(IEventHandler eh, IMQMQ mq) {
      this.mq = mq;
      this.eh = eh;
      preproc = new LinkedList<StratconMessage>();
      alternates = new LinkedList<MessageHandler>();
    }

    public void addObserver(MessageHandler mh) {
      alternates.add(mh);
    }
    public void preprocess(StratconMessage m) throws Exception {
      if(booted) throw new Exception("Already booted");
      preproc.add(m);
    }

    protected void process(IEventHandler eh, List<StratconMessage> l) {
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
    public IEventHandler getEventHandler() { return eh; }
    public IMQMQ getMQ() { return mq; }
    public void run() {
      for ( MessageHandler mh : alternates ) eh.addObserver(mh);
      process(eh, preproc);
      booted();
      while(true) {
        try {
          mq.connect();
          try { mq.consume(eh); } catch (Exception anything) {}
          mq.disconnect();
        }
        catch (Exception e) {
          Throwable cause = e.getCause();
          if(cause == null) cause = e;
          System.err.println("MQ connection failed: " + cause.getMessage());
        }
        try { Thread.sleep(1000); } catch (InterruptedException ignore) {}
      }
    }
}
