/*
 * Copyright (c) 2012, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

public interface NoitMetricGeneric {
  public String getPrefix();
  public String getUuid();
  public long getTime();
  public String getName();
  public String getNoit();
  public String getCheck_target();
  public String getCheck_module();
  public String getCheck_name();
  public int numparts();
}

