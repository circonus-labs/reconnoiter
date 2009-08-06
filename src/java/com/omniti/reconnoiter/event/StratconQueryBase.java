/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import java.lang.System;

import com.omniti.reconnoiter.StratconMessage;
import com.espertech.esper.client.EPStatement;
import com.espertech.esper.client.UpdateListener;
import java.util.UUID;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathFactory;
import javax.xml.xpath.XPathConstants;
import javax.xml.xpath.XPathExpressionException;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

public class StratconQueryBase extends StratconMessage {
  protected EPStatement statement;
  protected UUID uuid;
  protected String expression;

  public UUID getUUID() {
    return uuid;
  }
  public EPStatement getStatement() {
    return statement;
  }
  public String getExpression() {
    return expression;
  }
  public void setStatement(EPStatement s) {
    this.statement = s;
  }
  public void destroy() {
    statement.destroy();
  }
}
