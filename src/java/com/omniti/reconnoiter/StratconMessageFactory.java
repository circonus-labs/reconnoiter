/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter;

import com.omniti.reconnoiter.StratconMessage;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;

public class StratconMessageFactory {
  private Class clazz;
  private Constructor<StratconMessage> con;
  private int len;

  @SuppressWarnings("unchecked")
  public StratconMessageFactory(Class clazz)
         throws NoSuchMethodException, IllegalAccessException,
                InvocationTargetException, InstantiationException {
    Method meth;

    this.clazz = clazz;
    con = clazz.getDeclaredConstructor( new Class[] { String[].class } );
    meth = clazz.getMethod("getLength");
    int len = (Integer)meth.invoke(clazz.newInstance());
    this.con = con;
    this.len = len;
  }
  StratconMessage make(String[] args)
                  throws InstantiationException, IllegalAccessException,
                         InvocationTargetException {
    return con.newInstance((Object)args);
  }
  int getLength() { return len; }
} 

