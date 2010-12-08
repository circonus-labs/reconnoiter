/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.EventHandler;
import com.omniti.reconnoiter.StratconMessage;

public abstract class NoitEvent extends StratconMessage {
  public NoitEvent() {}
  public NoitEvent(String[] parts) throws Exception {
    super(parts);
  }
  public String[] extended_id_split(String id) {
    String parts[] = { null, null, null, null }; /* target,module,name,uuid */
    if(id.length() <= 36) {
      parts[3] = id;
      return parts;
    }
    parts[3] = id.substring(id.length() - 36);
    int p1 = id.indexOf('`');
    if(p1 < 0) return parts;
    parts[0] = id.substring(0,p1);
    int p2 = id.indexOf('`', p1+1);
    if(p2 < 0) return parts;
    parts[1] = id.substring(p1+1,p2);
    parts[2] = id.substring(p2+1,id.length()-37);
    return parts;
  }
  public void handle(EventHandler eh) {
    eh.getService().getEPRuntime().sendEvent(this);
  }
}
