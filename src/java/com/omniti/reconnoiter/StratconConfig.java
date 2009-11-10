/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import java.io.File;
import java.io.IOException;
import java.util.Properties;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathExpression;
import javax.xml.xpath.XPathExpressionException;
import javax.xml.xpath.XPathFactory;
import javax.xml.xpath.XPathConstants;
import org.xml.sax.SAXException;

import com.espertech.esper.client.ConfigurationDBRef;
import org.apache.commons.dbcp.BasicDataSourceFactory;
import org.w3c.dom.Document;
import org.w3c.dom.NodeList;


public class StratconConfig {
  private Document doc;
  
  


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
  
  public String getStompParameter(String param, String or) {
    String result = getStompParameter(param);
    if (result == null)
      return or;
    return result;
  }
  
  public String getStompParameter(String param) {
    return getIepParameter("stomp", param);
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
  public ConfigurationDBRef getDBConfig() {
    Properties props = new Properties();
    props.put("username", getDatabaseParameter("user"));
    props.put("password", getDatabaseParameter("password"));
    props.put("driverClassName", "org.postgresql.Driver");
    props.put("url", "jdbc:postgresql://" + getDatabaseParameter("host") +
                                      "/" + getDatabaseParameter("dbname"));
    props.put("initialSize", 2);
    props.put("validationQuery", "select 1");

    ConfigurationDBRef configDB = new ConfigurationDBRef();
    configDB.setDataSourceFactory(props, BasicDataSourceFactory.class.getName());
    configDB.setConnectionLifecycleEnum(ConfigurationDBRef.ConnectionLifecycleEnum.POOLED);
    configDB.setExpiryTimeCache(60, 120);
    configDB.setColumnChangeCase(ConfigurationDBRef.ColumnChangeCaseEnum.LOWERCASE);
    // The UUID needs some help mapping
    configDB.addSqlTypesBinding(1111, "String");
    return configDB;
  }
}
