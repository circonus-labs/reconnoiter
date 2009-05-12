package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.StratconMessage;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.ConfigurationEventTypeXMLDOM;

import javax.xml.xpath.XPathConstants;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;

public class NoitEvent extends StratconMessage {
  protected Document document;

  public static void registerTypes(EPServiceProvider epService) {
    ConfigurationEventTypeXMLDOM cfg;

    cfg = new ConfigurationEventTypeXMLDOM();
    cfg.addXPathProperty("uuid", "/NoitStatus/id", XPathConstants.STRING);
    cfg.addXPathProperty("status", "/NoitStatus/status", XPathConstants.STRING);
    cfg.addXPathProperty("state", "/NoitStatus/state", XPathConstants.STRING);
    cfg.addXPathProperty("availability", "/NoitStatus/availability", XPathConstants.STRING);
    cfg.addXPathProperty("duration", "/NoitStatus/duration", XPathConstants.NUMBER);
    cfg.setRootElementName("NoitStatus");
    epService.getEPAdministrator().getConfiguration()
             .addEventType("NoitStatus", cfg);

    cfg = new ConfigurationEventTypeXMLDOM();
    cfg.addXPathProperty("uuid", "/NoitStatus/id", XPathConstants.STRING);
    cfg.addXPathProperty("name", "/NoitStatus/name", XPathConstants.STRING);
    cfg.addXPathProperty("value", "/NoitStatus/value", XPathConstants.NUMBER);
    cfg.setRootElementName("NoitMetricNumeric");
    epService.getEPAdministrator().getConfiguration()
             .addEventType("NoitMetricNumeric", cfg);

    cfg = new ConfigurationEventTypeXMLDOM();
    cfg.addXPathProperty("uuid", "/NoitStatus/id", XPathConstants.STRING);
    cfg.addXPathProperty("name", "/NoitStatus/name", XPathConstants.STRING);
    cfg.addXPathProperty("message", "/NoitStatus/message", XPathConstants.STRING);
    cfg.setRootElementName("NoitMetricText");
    epService.getEPAdministrator().getConfiguration()
             .addEventType("NoitMetricText", cfg);
  }

  public NoitEvent(Document d) {
    document = d;
  }
  public Document getDocument() {
    return document;
  }
}

