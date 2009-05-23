/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.w3c.dom.Text;

public class NoitMetricNumeric extends StratconMessage {
    private String uuid;
    private String name;
    private double value;
    private String remote;

    protected String get_string(Element e, String tag) throws NoitMetricNumericException {
      NodeList vals = e.getElementsByTagName(tag);
      if(vals.getLength() != 1)
        throw new NoitMetricNumericException("Bad XML: tag " + tag + " (" + vals.getLength() + ")");
      Node n = vals.item(0);
      Node tn = n.getFirstChild();
      if(tn.getNextSibling() != null)
        throw new NoitMetricNumericException("Bad XML: " + tag + " has siblings");
      if(tn.getNodeType() != Node.TEXT_NODE)
        throw new NoitMetricNumericException("Bad XML: " + tag + " not text");
      Text text_node = (Text)tn;
      return text_node.getNodeValue();
    }
    protected Double get_double(Element e, String tag) throws NoitMetricNumericException {
      Double d = null;
      try {
        d = new Double(get_string(e, tag));
      } catch(NumberFormatException nfe) {
      }
      return d;
    }

    public NoitMetricNumeric(Document document) throws NoitMetricNumericException {
      Element e = document.getDocumentElement();
      String tag = e.getTagName();
      if(!tag.equals("NoitMetricNumeric"))
        throw new NoitMetricNumericException("Bad XML");
      uuid = get_string(e,"id");
      name = get_string(e,"name");
      value = get_double(e, "value");
      remote = get_string(e,"remote");
    }

    public String getuuid() { return uuid; }
    public String getname() { return name; }
    public double getvalue() { return value; }
    public String getremote() { return remote; }

    private class NoitMetricNumericException extends Exception {
      public NoitMetricNumericException(String s) {
        super(s);
      }
    }
}
