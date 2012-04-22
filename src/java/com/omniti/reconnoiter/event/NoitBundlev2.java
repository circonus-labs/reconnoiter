/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import java.util.LinkedList;
import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.event.NoitEvent;
import com.omniti.reconnoiter.event.NoitStatus;
import com.omniti.reconnoiter.event.NoitMetric;
import com.omniti.reconnoiter.CheckStatus;
import org.apache.commons.codec.binary.Base64;

public class NoitBundlev2 extends NoitEvent {
  LinkedList<NoitEvent> items;

  public String getPrefix() {
    return "B2";
  }

  protected byte[] decompress(byte input[], int len) {
    return input;
  }
  protected byte[] unwrap(String input, int len) {
    return decompress(Base64.decodeBase64(input), len);
  }
  protected CheckStatus.Bundle decode(String input, int len) {
    try {
      return CheckStatus.Bundle.parseFrom(unwrap(input, len));
    } catch(com.google.protobuf.InvalidProtocolBufferException e) {
    }
    return null;
  }
  /*
   'B#' REMOTE TIMESTAMP UUID TARGET MODULE NAME RAWLEN ENCODED
   */
  public NoitBundlev2() {}
  public NoitBundlev2(String[] parts) throws Exception {
    super(parts);
    items = new LinkedList<NoitEvent>();
    String id[] = extended_id_split(parts[3]);
    String noit = parts[1];
    String timestamp = parts[2];
    String uuid = id[3];
    String target = parts[4];
    String module = parts[5];
    String name = parts[6];
    int rawlen = Integer.parseInt(parts[7]);
    CheckStatus.Bundle bundle = decode(parts[8], rawlen);
    if(bundle != null) {
      CheckStatus.Status status = bundle.getStatus();
      if(status != null) {
        char state[] = new char [] { (char)status.getState() };
        char available[] = new char [] { (char) status.getAvailable() };

        items.addLast(new NoitStatus(new java.lang.String[]
                      { "S", noit, timestamp, parts[3], new String(state),
                        new String(available),
                        new Integer(status.getDuration()).toString(),
                        status.getStatus() }));
      }
      for(CheckStatus.Metric metric : bundle.getMetricsList()) {
        char metrictype[] = new char [] { (char)metric.getMetricType() };
        String v_str = null;
        switch(metrictype[0]) {
          case 'i':
            if(metric.hasValueI32())
              v_str = Integer.toString(metric.getValueI32());
            break;
          case 'I':
            if(metric.hasValueUI32())
              v_str = Long.toString(metric.getValueUI32());
            break;
          case 'l':
            if(metric.hasValueI64())
              v_str = Long.toString(metric.getValueI64());
            break;
          case 'L':
            if(metric.hasValueUI64())
              v_str = Double.toString(metric.getValueUI64());
            break;
          case 'n':
            if(metric.hasValueDbl())
              v_str = Double.toString(metric.getValueDbl());
            break;
          case 's': v_str = metric.getValueStr();
          default:
            break;
        }
        if(v_str == null) v_str = "[[null]]";
        items.addLast(new NoitMetric(new java.lang.String[]
                      { "M", noit, timestamp, parts[3], metric.getName(),
                        new String(metrictype), v_str }));
      }
    }
  }

  public int numparts() { return 9; }
  public void handle(EventHandler eh) {
    for(NoitEvent e : items) {
      e.handle(eh);
    }
  }
}
