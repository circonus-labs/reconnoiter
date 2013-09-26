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

package com.omniti.reconnoiter.event;

import java.util.LinkedList;
import com.omniti.reconnoiter.IEventHandler;
import com.omniti.reconnoiter.event.NoitEvent;
import com.omniti.reconnoiter.event.NoitStatus;
import com.omniti.reconnoiter.event.NoitMetric;
import com.omniti.reconnoiter.CheckStatus;
import org.apache.commons.codec.binary.Base64;

public class NoitBundlev2 extends NoitEvent {
  LinkedList<NoitEvent> items;
  private long time;
  private String noit;
  private String uuid;
  private String check_target;
  private String check_module;
  private String check_name;

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
    noit = parts[1];
    String timestamp = parts[2];
    time = timeToLong(timestamp);
    check_target = id[0];
    check_module = id[1];
    check_name = id[2];
    uuid = id[3];
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

  public String getUuid() { return uuid; }
  public long getTime() { return time; }
  public String getNoit() { return noit; }
  public String getCheck_target() { return check_target; }
  public String getCheck_module() { return check_module; }
  public String getCheck_name() { return check_name; }

  public int numparts() { return 9; }
  public void handle(IEventHandler eh) {
    for(NoitEvent e : items) {
      if (eh.stopProcessing(e, getPrefix()) == false)
        e.handle(eh);
    }
  }
}
