package com.omniti.jezebel;

import java.io.*;
import org.xml.sax.*;
import org.xml.sax.helpers.*;
import javax.xml.parsers.*;
import javax.xml.transform.*;
import javax.xml.transform.stream.*;
import javax.xml.transform.sax.*; 
import javax.servlet.http.HttpServletResponse;
import java.util.Stack;
import com.omniti.jezebel.ResmonResult;

public class Resmon {
    private Stack<ResmonResult> s;
    public Resmon() {
        s = new Stack<ResmonResult>();
    }
    public ResmonResult addResult(String module, String service) {
        ResmonResult r = new ResmonResult(module, service);
        s.push(r);
        return r;
    }
    public void write(HttpServletResponse response) {
        StreamResult streamResult;
        SAXTransformerFactory tf;
        TransformerHandler hd;
        Transformer serializer;

        try {
            try {
                streamResult = new StreamResult(response.getWriter());
                tf = (SAXTransformerFactory) SAXTransformerFactory.newInstance();
                hd = tf.newTransformerHandler();
                serializer = hd.getTransformer();
        
                serializer.setOutputProperty(OutputKeys.ENCODING,"utf-8");
                serializer.setOutputProperty(OutputKeys.DOCTYPE_SYSTEM,"http://labs.omniti.com/resmon/trunk/resources/resmon.dtd");
                serializer.setOutputProperty(OutputKeys.INDENT,"yes");
                
                hd.setResult(streamResult);
                hd.startDocument();
                AttributesImpl atts = new AttributesImpl();
                hd.startElement("","","ResmonResults",atts);
                for ( ResmonResult r : s ) {
                    r.write(hd);
                }
                hd.endElement("","","ResmonResults");
                hd.endDocument();
            }
            catch(TransformerConfigurationException tce) {
                response.getWriter().println(tce.getMessage());
            }
            catch(SAXException se) {
                response.getWriter().println(se.getMessage());
            }
        }
        catch(IOException ioe) {
        }
    }
}
