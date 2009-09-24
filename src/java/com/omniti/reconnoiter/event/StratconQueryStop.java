/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;
import java.util.UUID;
import org.w3c.dom.Document;
import org.w3c.dom.Element;

public class StratconQueryStop extends StratconQueryBase {

   protected String getPrefix() {
     return "q";
   }

/*  'q' REMOTE ID */
  public StratconQueryStop(String[] parts) throws Exception {
    super(parts);
    uuid = UUID.fromString(parts[2]);
  }
  public UUID getUUID() {
    return uuid;
  }

  protected int getLength() {
    return 3;
  }
  
}

