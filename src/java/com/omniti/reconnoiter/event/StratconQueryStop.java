package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.StratconMessage;
import java.util.UUID;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;

public class StratconQueryStop extends StratconMessage {
  private UUID uuid;

  public StratconQueryStop(Document d) {
    Element e = d.getDocumentElement();
    uuid = UUID.fromString(e.getTextContent());
  }
  public UUID getUUID() {
    return uuid;
  }
  
}

