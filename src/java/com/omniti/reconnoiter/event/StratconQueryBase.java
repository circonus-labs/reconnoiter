/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;


import com.omniti.reconnoiter.StratconMessage;
import com.espertech.esper.client.EPStatement;
import java.util.UUID;

public abstract class StratconQueryBase extends StratconMessage {
  protected EPStatement statement;
  protected UUID uuid;
  protected String expression;

  public StratconQueryBase() {}
  public StratconQueryBase(String parts[]) throws Exception {
    super(parts);
  }

  public UUID getUUID() {
    return uuid;
  }
  public EPStatement getStatement() {
    return statement;
  }
  public String getExpression() {
    return expression;
  }
  public void setStatement(EPStatement s) {
    this.statement = s;
  }
  public void destroy() {
    statement.destroy();
  }
}
