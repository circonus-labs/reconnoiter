package com.omniti.reconnoiter;

import java.io.StringReader;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import org.xml.sax.InputSource;
import org.xml.sax.SAXException;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;

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
         tag.equals("NoitMetricNumeric") ||
         tag.equals("NoitMetricText"))
        return new NoitEvent(document);
      // and requests
      else if(tag.equals("StratconQuery"))
        return new StratconQuery(document);
      else if(tag.equals("StratconQueryStop"))
        return new StratconQueryStop(document);
    }
    catch(Exception e) {
    }
    return null;
  }
}
