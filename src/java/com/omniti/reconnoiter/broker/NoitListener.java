/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.broker;

import java.util.concurrent.LinkedBlockingQueue;
import com.espertech.esper.client.EventBean;
import com.espertech.esper.client.UpdateListener;

public class NoitListener implements UpdateListener {
    protected LinkedBlockingQueue<EventBean> queue;
    public NoitListener() {
        queue = new LinkedBlockingQueue<EventBean>();
    }
    public void update(EventBean[] newEvents, EventBean[] oldEvents) {
        System.err.println("AMQOutput -> dispatch");
        for(int i = 0; i < newEvents.length; i++) {
            boolean done = false;
            while(!done) {
                try {
                    queue.put(newEvents[i]);
                    done = true;
                }
                catch(InterruptedException e) {
                }
            }
        }
    }
}
