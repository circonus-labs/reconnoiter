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
