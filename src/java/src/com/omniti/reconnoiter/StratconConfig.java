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

import com.espertech.esper.client.ConfigurationDBRef;
import org.apache.commons.dbcp.BasicDataSourceFactory;
import org.w3c.dom.Document;
import org.w3c.dom.NodeList;
import org.w3c.dom.Node;

import com.omniti.reconnoiter.StratconMessage;
import com.omniti.reconnoiter.event.StratconStatement;
import com.omniti.reconnoiter.event.StratconQuery;

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

  public List<StratconMessage> getQueries() throws Exception {
    LinkedList<StratconMessage> list = new LinkedList<StratconMessage>();
    Hashtable<String,StatementNode> stmt_by_id = new Hashtable<String,StatementNode>();
    Hashtable<String,StatementNode> stmt_by_provider = new Hashtable<String,StatementNode>();
    XPathFactory factory = XPathFactory.newInstance();
    XPath xpath = factory.newXPath();
    XPathExpression expr_epl, expr_req;
    Object result;
    NodeList queries;
    try {
      XPathExpression expr =
        xpath.compile("/stratcon/iep/queries[@master=\"iep\"]//statement");
      result = expr.evaluate(doc, XPathConstants.NODESET);

      XPathExpression qexpr =
        xpath.compile("/stratcon/iep/queries[@master=\"iep\"]//query");
      queries = (NodeList) qexpr.evaluate(doc, XPathConstants.NODESET);

      expr_epl = xpath.compile("self::node()/epl");
      expr_req = xpath.compile("self::node()/requires");
    }
    catch(XPathExpressionException e) {
      System.err.println("Bad expression: " + e.getMessage());
      return null;
    }
    NodeList nodes = (NodeList) result;

    /* Phase 1: sweep in all the statements */
    for(int i = 0; i < nodes.getLength(); i++) {
      StatementNode stmt = new StatementNode();
      Node node = nodes.item(i);
      Node node_id = node.getAttributes().getNamedItem("id");
      if(node_id == null) continue;
      stmt.id = node_id.getNodeValue();
      Node provides_id = node.getAttributes().getNamedItem("provides");
      if(provides_id != null) stmt.provides = provides_id.getNodeValue();
      NodeList nodes_epl = (NodeList) expr_epl.evaluate(node, XPathConstants.NODESET);
      if(nodes_epl.getLength() != 1) continue;
      stmt.statement = nodes_epl.item(0).getTextContent();
      stmt_by_id.put(stmt.id, stmt);
      if(stmt.provides != null)
        if(stmt_by_provider.put(stmt.provides, stmt) != null)
          throw new Exception("Two statements provide: " + stmt.provides);
    }

    /* Phase 2: load the requires graph */
    for(int i = 0; i < nodes.getLength(); i++) {
      StatementNode stmt;
      Node node = nodes.item(i);
      Node node_id = node.getAttributes().getNamedItem("id");
      if(node_id == null) continue;
      String id = node_id.getNodeValue();
      stmt = stmt_by_id.get(id);
      if(stmt == null) throw new Exception("Cannot find statement: " + id);
      NodeList nodes_req = (NodeList) expr_req.evaluate(node, XPathConstants.NODESET);
      stmt.nrequires = nodes_req.getLength();
      if(stmt.nrequires > 0) {
        stmt.requires = new StatementNode[stmt.nrequires];
        for(int j = 0; j < stmt.nrequires; j++) {
          String req = nodes_req.item(j).getTextContent();
          StatementNode stmt_req = stmt_by_provider.get(req);
          if(stmt_req == null) throw new Exception("Statement " + stmt.id + " requires " + req + " which no one provides.");
          stmt.requires[j] = stmt_req;
        }
      }
    }

    /* Phase 3: Recursive sweep and mark to detect cycles.
       We're walking the graph backwards here from dependent to provider,
       but a cycle is a cycle, so this validates the graph. */
    int mgen = 0;
    for ( StatementNode stmt : stmt_by_id.values() )
      if(stmt_mark_dag(stmt, ++mgen) < 0) throw new Exception("Statement " + stmt.id + " has cyclic requirements.");

    /* Phase 4: clean the markings */
    for ( StatementNode stmt : stmt_by_id.values() )
      stmt.marked = 0;

    /* Phase 5: do the load */
    for ( StatementNode stmt : stmt_by_id.values() )
      stmt_append_to_list(list, stmt);


    /* Phase 6: pull in the queries (which is much simpler) */
    for (int i=0; i<queries.getLength(); i++) {
      Node node = queries.item(i);
      Node node_id = node.getAttributes().getNamedItem("id");
      if(node_id == null) continue;
      String id = node_id.getNodeValue();
      Node node_topic = node.getAttributes().getNamedItem("topic");
      if(node_topic == null) continue;
      String topic = node_topic.getNodeValue();
      NodeList nodes_epl = (NodeList) expr_epl.evaluate(node, XPathConstants.NODESET);
      if(nodes_epl.getLength() != 1) continue;
      String statement = nodes_epl.item(0).getTextContent();
      list.add(new StratconQuery(new String[] { "Q", "", id, topic, statement}));
    }
    return list;
  }

  protected void stmt_append_to_list(List<StratconMessage> l, StatementNode stmt) throws Exception {
    if(stmt.marked > 0) return;
    for(int i=0; i<stmt.nrequires; i++)
      stmt_append_to_list(l, stmt.requires[i]);

    l.add(new StratconStatement(new String [] { "D", "", stmt.id, stmt.statement }));

    stmt.marked = 1;
  }

  protected int stmt_mark_dag(StatementNode stmt, int mgen) {
    if(stmt.marked >= mgen) return -1;
    if(stmt.marked > 0) return 0;
    stmt.marked = mgen;
    for(int i=0; i<stmt.nrequires; i++)
      if(stmt_mark_dag(stmt.requires[i], mgen) < 0) return -1;
    return 0;
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
