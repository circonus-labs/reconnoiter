/*
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

package com.omniti.jezebel;

import java.io.*;
import org.xml.sax.*;
import org.xml.sax.helpers.*;
import javax.xml.parsers.*;
import javax.xml.transform.*;
import javax.xml.transform.stream.*;
import javax.xml.transform.sax.*;
import javax.servlet.http.HttpServletResponse;
import java.util.Stack;
import java.util.Hashtable;
import java.util.Map;

public class ResmonResult {
    private String module;
    private String service;
    private long last_update;
    private Hashtable<String,ResmonMetricData> metrics;

    protected class ResmonMetricData {
        public String type;
        public String value;
        public ResmonMetricData(String t, String v) { type = t; value = v; }
        public ResmonMetricData(Integer v) { type = "i"; value = (v == null) ? null : ("" + v); }
        public ResmonMetricData(Long v) { type = "l"; value = (v == null) ? null : ("" + v); }
        public ResmonMetricData(Double v) { type = "n"; value = (v == null) ? null : ("" + v); }
        public ResmonMetricData(Float v) { type = "n"; value = (v == null) ? null : ("" + v); }
        public ResmonMetricData(String v) { type = "s"; value = v; }
    }

    public ResmonResult(String m, String s) {
        module = m;
        service = s;
        last_update = System.currentTimeMillis() / 1000;
        metrics = new Hashtable<String,ResmonMetricData>();
    }

    public ResmonResult addMetric(String name, ResmonMetricData d) {
        synchronized(metrics) {
            metrics.remove(name);
            metrics.put(name, d);
        }
        last_update = System.currentTimeMillis() / 1000;
        return this;
    }
    public ResmonResult set(String name, String t, String v) { return addMetric(name, new ResmonMetricData(t,v)); }
    public ResmonResult set(String name, Integer v) { return addMetric(name, new ResmonMetricData(v)); }
    public ResmonResult set(String name, Long v) { return addMetric(name, new ResmonMetricData(v)); }
    public ResmonResult set(String name, Double v) { return addMetric(name, new ResmonMetricData(v)); }
    public ResmonResult set(String name, Float v) { return addMetric(name, new ResmonMetricData(v)); }
    public ResmonResult set(String name, String v) { return addMetric(name, new ResmonMetricData(v)); }
    public ResmonResult clear() { synchronized(metrics) { metrics.clear(); } return this; }

    public void write(TransformerHandler hd) throws SAXException {
        AttributesImpl atts = new AttributesImpl();
        atts.addAttribute("","","module","CDATA",module);
        atts.addAttribute("","","service","CDATA",service);
        hd.startElement("","","ResmonResult",atts);
        atts.clear();
        hd.startElement("","","last_update",atts);
        String epochString = "" + last_update;
        char epochChars[] = epochString.toCharArray();
        hd.characters(epochChars, 0, epochChars.length);
        hd.endElement("","","last_update");
        synchronized(metrics) {
            for (Map.Entry<String,ResmonMetricData> e : metrics.entrySet()) {
                ResmonMetricData d = e.getValue();
                atts.clear();
                atts.addAttribute("","","name","CDATA",e.getKey());
                atts.addAttribute("","","type","CDATA",d.type);
                hd.startElement("","","metric",atts);
                char valueChars[] = d.value.toCharArray();
                hd.characters(valueChars, 0, valueChars.length);
                hd.endElement("","","metric");
            }
        }
        hd.endElement("","","ResmonResult");
    }
}
