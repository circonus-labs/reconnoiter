/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;


import com.omniti.reconnoiter.event.StratconQueryBase;
import com.espertech.esper.client.UpdateListener;
import java.util.UUID;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathFactory;
import javax.xml.xpath.XPathConstants;
import javax.xml.xpath.XPathExpressionException;
import org.w3c.dom.Document;

public class StratconQuery extends StratconQueryBase {
  protected UpdateListener listener;
  protected String name;

  public StratconQuery(Document d) {
    XPath xpath = XPathFactory.newInstance().newXPath();
    try {
      String id = (String) xpath.evaluate("/StratconQuery/id", d, XPathConstants.STRING);
      if(id == null) uuid = UUID.randomUUID();
      else uuid = UUID.fromString(id);
      name = (String) xpath.evaluate("/StratconQuery/name", d, XPathConstants.STRING);
      expression = (String) xpath.evaluate("/StratconQuery/expression", d, XPathConstants.STRING);
    }
    catch(XPathExpressionException e) {
    }
    if(name == null) name = "default";
    if(uuid == null) uuid = UUID.randomUUID();
  }
  public String getName() {
    return name;
  }
  public void setListener(UpdateListener l) {
    this.listener = l;
  }
  public void destroy() {
    statement.removeListener(listener);
    statement.destroy();
  }
}
