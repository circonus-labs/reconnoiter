/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import java.io.StringReader;
import java.util.HashMap;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;
import com.omniti.reconnoiter.event.*;

public abstract class StratconMessage {
  // This is the text type in the noit_log.h
  public final static String METRIC_STRING = "s";
  public final static HashMap<String,StratconMessageFactory> quicklookup = new HashMap<String,StratconMessageFactory>();

  public static boolean registerType(Class clazz) {
    boolean success = false;
    try {
      Method meth = clazz.getMethod("getPrefix");
      String prefix = (String)meth.invoke(clazz.newInstance());
      StratconMessageFactory smf = new StratconMessageFactory(clazz);
      quicklookup.put(prefix, smf);
      success = true;
    } catch (NoSuchMethodException e) {
      e.printStackTrace();
    } catch (IllegalAccessException e) {
      e.printStackTrace();
    } catch (InvocationTargetException e) {
      e.printStackTrace();
    } catch (InstantiationException e) {
      e.printStackTrace();
    }
    return success;
  }  
  public static String[] parseToArray(String jlog, int num) {
    // Get rid of the null parameter
    String operand = jlog.endsWith("\n") ? jlog.substring(0, jlog.length()-1)
                                         : jlog;
    return operand.split("[\t]", num);
  }

  public String getPrefix() { return null; }

  protected long timeToLong(String time) {
    long ms = 0;
    int off = time.lastIndexOf('.');
    if(off == -1) return 0;
    try {
      ms = Long.valueOf(time.substring(0,off)).longValue() * 1000;
      ms = ms + Long.valueOf(time.substring(off+1)).longValue();
    }
    catch (NumberFormatException e) {
      return 0;
    }
    return ms;
  }

  public int getLength() { return 0; }

  public abstract void handle(EventHandler eh);

  public StratconMessage() {}
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
    if(jlog == null || jlog.length() == 0) return null;
    // The numbers of the parse are pulled from stratcon and
    // +1 for the extra remote
    try {
      String prefix = jlog.substring(0, jlog.indexOf('\t'));

      StratconMessageFactory smf = quicklookup.get(prefix);
      if(smf == null) {
        throw new Exception("no handler for " + jlog);
      }
      parts = parseToArray(jlog, smf.getLength());
      return smf.make(parts);
    }
    catch(Exception e) {
      System.err.println("makeMessage: " + e);
      e.printStackTrace();
    }
    return null;
  }
  public static StratconMessage[] makeMessages(String big) {
    String[] lines = big.split("[\n]");
    StratconMessage[] messages = new StratconMessage[lines.length];
    for (int i = 0; i < messages.length; i++) {
      messages[i] = makeMessage(lines[i]);
    }
    return messages;
  }
  static {
    StratconMessage.registerType(NoitCheck.class);
    StratconMessage.registerType(NoitStatus.class);
    StratconMessage.registerType(NoitMetric.class);
    StratconMessage.registerType(StratconStatement.class);
    StratconMessage.registerType(StratconQuery.class);
    StratconMessage.registerType(StratconQueryStop.class);
  }
}
