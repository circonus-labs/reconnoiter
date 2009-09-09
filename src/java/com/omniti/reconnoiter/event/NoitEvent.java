/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;

import com.espertech.esper.client.EPServiceProvider;
import com.espertech.esper.client.ConfigurationEventTypeXMLDOM;

import javax.xml.xpath.XPathConstants;

import org.w3c.dom.Document;

public class NoitEvent extends StratconMessage {
  protected Document document;

  public static void registerTypes(EPServiceProvider epService) {
    ConfigurationEventTypeXMLDOM cfg;

    cfg = new ConfigurationEventTypeXMLDOM();
    cfg.addXPathProperty("uuid", "/NoitCheck/id", XPathConstants.STRING);
    cfg.addXPathProperty("target", "/NoitCheck/target", XPathConstants.STRING);
    cfg.addXPathProperty("module", "/NoitCheck/module", XPathConstants.STRING);
    cfg.addXPathProperty("name", "/NoitCheck/name", XPathConstants.STRING);
    cfg.addXPathProperty("noit", "/NoitCheck/remote", XPathConstants.STRING);
    cfg.setRootElementName("NoitCheck");
    epService.getEPAdministrator().getConfiguration()
             .addEventType("NoitCheck", cfg);

    cfg = new ConfigurationEventTypeXMLDOM();
    cfg.addXPathProperty("uuid", "/NoitStatus/id", XPathConstants.STRING);
    cfg.addXPathProperty("status", "/NoitStatus/status", XPathConstants.STRING);
    cfg.addXPathProperty("state", "/NoitStatus/state", XPathConstants.STRING);
    cfg.addXPathProperty("availability", "/NoitStatus/availability", XPathConstants.STRING);
    cfg.addXPathProperty("duration", "/NoitStatus/duration", XPathConstants.NUMBER);
    cfg.addXPathProperty("noit", "/NoitStatus/remote", XPathConstants.STRING);
    cfg.setRootElementName("NoitStatus");
    epService.getEPAdministrator().getConfiguration()
             .addEventType("NoitStatus", cfg);
    cfg = new ConfigurationEventTypeXMLDOM();
    cfg.addXPathProperty("uuid", "/NoitMetricText/id", XPathConstants.STRING);
    cfg.addXPathProperty("name", "/NoitMetricText/name", XPathConstants.STRING);
    cfg.addXPathProperty("message", "/NoitMetricText/message", XPathConstants.STRING);
    cfg.addXPathProperty("noit", "/NoitMetricText/remote", XPathConstants.STRING);
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

