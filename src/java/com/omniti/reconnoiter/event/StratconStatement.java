/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.event.StratconQueryBase;
import java.util.UUID;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathFactory;
import javax.xml.xpath.XPathConstants;
import javax.xml.xpath.XPathExpressionException;
import org.w3c.dom.Document;

public class StratconStatement extends StratconQueryBase {
  public StratconStatement(Document d) {
    XPath xpath = XPathFactory.newInstance().newXPath();
    try {
      String id = (String) xpath.evaluate("/StratconStatement/id", d, XPathConstants.STRING);
      if(id == null) uuid = UUID.randomUUID();
      else uuid = UUID.fromString(id);
      expression = (String) xpath.evaluate("/StratconStatement/expression", d, XPathConstants.STRING);
    }
    catch(XPathExpressionException e) {
    }
    if(uuid == null) uuid = UUID.randomUUID();
  }
}
