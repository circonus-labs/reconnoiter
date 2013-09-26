/*
 * Copyright (c) 2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
