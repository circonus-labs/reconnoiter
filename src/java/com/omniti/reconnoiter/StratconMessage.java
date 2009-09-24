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

  // This is the text type in the noit_log.h
  public final static String METRIC_STRING = "s";
  
  public static String[] parseToArray(String jlog, int num) {
    // Get rid of the null parameter
    return jlog.substring(0, jlog.length()-1).split("[\t]", num);
  }

  protected String getPrefix() {
     return null;
  }

  protected int getLength() {
     return -1;
  }

  // Check and make sure
  public StratconMessage(String[] parts) throws Exception {
    if (!parts[0].equals(this.getPrefix())) {
      throw new Exception("Incorrect state prefix:" + getPrefix() + " not applicable for " + getClass());
    }
    if (parts.length != getLength()) {
      throw new Exception("Incorrect amount of parts parsed, tossing message.");
    }
  }

  public static StratconMessage makeMessage(String jlog) {
    String[] parts;
    // The numbers of the parse are pulled from stratcon and
    // +1 for the extra remote
    try {
      switch (jlog.charAt(0)) {
        case 'C':
          parts = parseToArray(jlog, 7);
          return new NoitCheck(parts);
        case 'S':
          parts = parseToArray(jlog, 8);
          return new NoitStatus(parts);
        case 'M':
          parts = parseToArray(jlog, 7);
          if (parts[5].equals(METRIC_STRING)) {
            return new NoitMetricText(parts);
          } else {
            return new NoitMetricNumeric(parts);
          }
        case 'D':
          parts = parseToArray(jlog, 4);
          return new StratconStatement(parts);
        case 'Q':
          parts = parseToArray(jlog, 5);
          return new StratconQuery(parts);
        case 'q':
          parts = parseToArray(jlog, 3);
          return new StratconQueryStop(parts);
      }
    }
    catch(Exception e) {
      System.err.println("makeMessage: " + e);
      e.printStackTrace();
    }
    return null;
  }
}
