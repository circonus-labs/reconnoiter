/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import java.io.StringReader;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import org.xml.sax.InputSource;

import org.w3c.dom.Document;
import org.w3c.dom.Element;

import com.omniti.reconnoiter.event.*;

public class StratconMessage {
  private static DocumentBuilderFactory factory = null;
  private static DocumentBuilder parser = null;

  public static StratconMessage makeMessage(String xml) {
    if(factory == null) {
      factory = DocumentBuilderFactory.newInstance();
      factory.setIgnoringComments(true);
      factory.setCoalescing(true); // Convert CDATA to Text nodes
      factory.setNamespaceAware(false); // No namespaces: this is default
      factory.setValidating(false); // Don't validate DTD: also default
    }
    if(parser == null) {
      try {
        parser = factory.newDocumentBuilder();
      } catch(Exception e) { }
    }

    InputSource source = new InputSource(new StringReader(xml));

    try {
      Document document = parser.parse(source);
      Element e = document.getDocumentElement();
      String tag = e.getTagName();
      // We have events
      if(tag.equals("NoitStatus") ||
         tag.equals("NoitMetricText") ||
         tag.equals("NoitCheck"))
        return new NoitEvent(document);
      else if(tag.equals("NoitMetricNumeric")) {
        // Numerics have a value that can be in scientific notation.
        // This document gets passed places that do Xpath 1.0 queries
        // which don't understand scientific notation... we have to hack it.
        return new NoitMetricNumeric(document);
      }
      // and requests
      else if(tag.equals("StratconStatement"))
        return new StratconStatement(document);
      else if(tag.equals("StratconQuery"))
        return new StratconQuery(document);
      else if(tag.equals("StratconQueryStop"))
        return new StratconQueryStop(document);
    }
    catch(Exception e) {
      System.err.println("makeMessage: " + e);
      e.printStackTrace();
    }
    return null;
  }
}
