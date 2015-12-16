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

import com.omniti.reconnoiter.IEventHandler;
import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.NoitMetricNumeric;
import com.omniti.reconnoiter.event.NoitMetricText;
import org.apache.log4j.Logger;

public class NoitMetric extends NoitEvent 
       implements NoitMetricGeneric {
  static Logger logger = Logger.getLogger(NoitMetric.class.getName());
  public final static String METRIC_STRING = "s";

  private NoitMetricText nmt;
  private NoitMetricNumeric nmn;
 
  public NoitMetric() {}
  public NoitMetric(String[] parts, NoitMetricText _nmt) throws Exception {
    super(parts);
    nmn = null;
    nmt = _nmt;
  }
  public NoitMetric(String[] parts, NoitMetricNumeric _nmn) throws Exception {
    super(parts);
    nmn = _nmn;
    nmt = null;
  }
  public NoitMetric(String[] parts) throws Exception {
    super(parts);

    // Transform an MT-record back to a usual M-record for construction
    if(parts[0].equals("MT")) {
       String m_record[] = new String[7];
       m_record[0] = "M";
       for(int i=1;i<7;i++) { m_record[i] = parts[i]; }
       parts = m_record;
    }

    if (parts[5].equals(METRIC_STRING)) {
      nmn = null;
      nmt = new NoitMetricText(parts);
    }
    else {
      nmn = new NoitMetricNumeric(parts);
      nmt = null;
    }
  }
  public void handle(IEventHandler eh) {
    long start = System.nanoTime();
    if(nmn != null) eh.sendEvent(nmn);
    if(nmt != null) eh.sendEvent(nmt);
    long nanos = System.nanoTime() - start;
    logger.debug("sendEvent("+getUuid()+"-"+getName()+") took "+(nanos/1000)+"us");
  }
  public String getUuid() { return (nmn != null)?nmn.getUuid():nmt.getUuid(); }
  public String getName() { return (nmn != null)?nmn.getName():nmt.getName(); }
  public long getTime() { return (nmn != null)?nmn.getTime():nmt.getTime(); }
  public String getNoit() { return (nmn != null)?nmn.getNoit():nmt.getNoit(); }
  public String getCheck_target() { return (nmn != null)?nmn.getCheck_target():nmt.getCheck_target(); }
  public String getCheck_module() { return (nmn != null)?nmn.getCheck_module():nmt.getCheck_module(); }
  public String getCheck_name() { return (nmn != null)?nmn.getCheck_name():nmt.getCheck_name(); }

  public boolean isNumeric() { return nmn != null; }
  public boolean isText() { return nmt != null; }
  public NoitMetricNumeric getNumeric() { return nmn; }
  public NoitMetricText getText() { return nmt; }
  public String getPrefix() { return "M"; }
  public int numparts() { return 7; }
}
