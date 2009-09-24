/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.event.StratconQueryBase;
import com.espertech.esper.client.UpdateListener;

import java.util.UUID;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathFactory;
import javax.xml.xpath.XPathConstants;
import javax.xml.xpath.XPathExpressionException;
import org.w3c.dom.Document;

public class StratconStatement extends StratconQueryBase {

   protected String getPrefix() {
     return "D";
   }

   /*  'D' REMOTE ID QUERY  */
   public StratconStatement(String[] parts) throws Exception {
      super(parts);
      String id = parts[2];
      expression = parts[3];
      if(id == null)
        uuid = UUID.randomUUID();
      else
        uuid = UUID.fromString(id);
   }

  protected int getLength() {
    return 4;
  }
}
