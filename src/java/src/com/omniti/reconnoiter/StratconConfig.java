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

package com.omniti.reconnoiter;

import java.io.File;
import java.io.IOException;
import java.util.Properties;
import java.util.List;
import java.util.LinkedList;
import java.util.Hashtable;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathExpression;
import javax.xml.xpath.XPathExpressionException;
import javax.xml.xpath.XPathFactory;
import javax.xml.xpath.XPathConstants;
import org.xml.sax.SAXException;

import org.apache.commons.dbcp.BasicDataSourceFactory;
import org.w3c.dom.Document;
import org.w3c.dom.NodeList;
import org.w3c.dom.Node;

import com.omniti.reconnoiter.StratconMessage;

public class StratconConfig {
  private Document doc;


  public class StatementNode {
    public String id;
    public String statement;
    public String provides;
    public int marked;
    public int nrequires;
    public StatementNode[] requires;
  }
  
  public StratconConfig(String filename) {
    DocumentBuilder docBuilder;
    DocumentBuilderFactory docBuilderFactory = DocumentBuilderFactory.newInstance();
    docBuilderFactory.setIgnoringElementContentWhitespace(true);
    try {
        docBuilder = docBuilderFactory.newDocumentBuilder();
    }
    catch (ParserConfigurationException e) {
        System.err.println("Could not create new DocumentBuilder: " + e.getMessage());
        return;
    }
    File file = new File(filename);
    try {
        doc = docBuilder.parse(file);
    }
    catch (SAXException e) {
        System.err.println("Bad XML: " + e.getMessage());
    }
    catch (IOException e) {
        System.out.println("Bad file: " + e.getMessage());
    }
  }

  public String getDatabaseParameter(String param) {
    XPathFactory factory = XPathFactory.newInstance();
    XPath xpath = factory.newXPath();
    Object result;
    try {
      XPathExpression expr =
        xpath.compile("/stratcon/database/dbconfig/" + param + "/text()");
      result = expr.evaluate(doc, XPathConstants.NODESET);
    }
    catch(XPathExpressionException e) {
      System.err.println("Bad expression: " + e.getMessage());
      return null;
    }
    NodeList nodes = (NodeList) result;
    if(nodes.getLength() > 0) {
      return nodes.item(nodes.getLength() -1).getNodeValue();
    }
    return null;
  }
  public String getBroker() {
    XPathFactory factory = XPathFactory.newInstance();
    XPath xpath = factory.newXPath();
    Object result;
    try {
      XPathExpression expr =
        xpath.compile("/stratcon/iep/broker/@adapter");
      result = expr.evaluate(doc, XPathConstants.NODESET);
    }
    catch(XPathExpressionException e) {
      System.err.println("Bad expression: " + e.getMessage());
      return null;
    }
    NodeList nodes = (NodeList) result;
    if(nodes.getLength() > 0) {
      return nodes.item(nodes.getLength() -1).getNodeValue();
    }
    return null;
  }
  
  public String getBrokerParameter(String param, String or) {
    String result = getBrokerParameter(param);
    if (result == null)
      return or;
    return result;
  }
  
  public String getBrokerParameter(String param) {
    return getIepParameter("broker", param);
  }
  
  public String getMQParameter(String param, String or) {
    String result = getMQParameter(param);
    if (result == null)
      return or;
    return result;
  }
  
  public String getMQParameter(String param) {
    return getIepParameter("mq", param);
  }
  
  public String getIepParameter(String which, String param) {
    XPathFactory factory = XPathFactory.newInstance();
    XPath xpath = factory.newXPath();
    Object result;
    try {
      XPathExpression expr =
        xpath.compile("/stratcon/iep/" + which + "/" + param + "/text()");
      result = expr.evaluate(doc, XPathConstants.NODESET);
    }
    catch(XPathExpressionException e) {
      System.err.println("Bad expression: " + e.getMessage());
      return null;
    }
    NodeList nodes = (NodeList) result;
    if(nodes.getLength() > 0) {
      String rv = nodes.item(nodes.getLength() -1).getNodeValue();
      return rv;
    }
    return null;
  }
}
