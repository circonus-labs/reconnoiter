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

package com.omniti.jezebel.check;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.Hashtable;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

import javax.management.MBeanServerConnection;
import javax.management.MBeanInfo;
import javax.management.MBeanAttributeInfo;
import javax.management.ObjectName;
import javax.management.ObjectInstance;
import javax.management.remote.JMXConnector;
import javax.management.remote.JMXConnectorFactory;
import javax.management.remote.JMXServiceURL;

import com.omniti.jezebel.ResmonResult;
import com.omniti.jezebel.JezebelCheck;
import com.omniti.jezebel.Jezebel;

public class jmx implements JezebelCheck {
    public jmx() {}
    public void perform(Map<String,String> check,
                        Map<String,String> config,
                        ResmonResult rr) 
    {
        final String host = check.remove("target");
        final String port = config.remove("port");
        final String username = config.remove("username");
        final String password = config.remove("password");
        final String mbean_domains = config.remove("mbean_domains");
        final String jmxGlassFishConnectorString =
            "service:jmx:rmi:///jndi/rmi://" + host + ":" + port + "/jmxrmi";
        try {
            final JMXServiceURL jmxUrl =
                new JMXServiceURL(jmxGlassFishConnectorString);
            final Map<String,String[]> jmxEnv = new HashMap<String, String[]>();
            final String[] credentials = new String[] {username, password};
            jmxEnv.put( JMXConnector.CREDENTIALS, credentials );
            final JMXConnector connector =
                JMXConnectorFactory.connect(jmxUrl, jmxEnv);
            final MBeanServerConnection mbsc = connector.getMBeanServerConnection();

            rr.set("default_domain", mbsc.getDefaultDomain());
            rr.set("mbean_count", mbsc.getMBeanCount());

            final Set<ObjectName> allObjectNames = mbsc.queryNames(null, null);

            ArrayList<String> domains = new ArrayList<String>();
            if ( mbean_domains != null ) {
                domains = new ArrayList<String>(Arrays.asList(mbean_domains.split("\\s+")));
            }

            Pattern space = Pattern.compile("\\s");
            
            for (ObjectName objectName : allObjectNames) {
                if ( ! domains.isEmpty() && ! domains.contains(objectName.getDomain()) ) {
                    continue;
                }

                MBeanInfo mbi = mbsc.getMBeanInfo(objectName);
                MBeanAttributeInfo[] attribs = mbi.getAttributes();
                String oname = "";

                Hashtable<String,String> objectPropList = objectName.getKeyPropertyList();
                if ( objectPropList.containsKey("name") ) {
                    oname += objectPropList.get("name");
                }
                if ( objectPropList.containsKey("type") ) {
                    if ( ! oname.equals("") ) {
                        oname += "`";
                    }
                    oname += objectPropList.get("type");
                }

                Matcher m = space.matcher(oname);
                oname = m.replaceAll("_");

                for (MBeanAttributeInfo attr : attribs)
                {
                    try {
                        String mname = attr.getName();
                        Matcher m2 = space.matcher(mname);
                        mname = m2.replaceAll("_");

                        if ( attr.getType().equals("java.lang.String") ) {
                            String val = (String)mbsc.getAttribute(objectName, attr.getName());
                            if ( val.length() <= 256 ) {
                                rr.set(oname + "`" + mname, val);
                            }
                        }
                        else if ( attr.getType().equals("int") ) {
                            rr.set(oname + "`" + mname, (Integer)mbsc.getAttribute(objectName, attr.getName()));
                        }
                        else if ( attr.getType().equals("long") ) {
                            rr.set(oname + "`" + mname, (Long)mbsc.getAttribute(objectName, attr.getName()));
                        }
                        else if ( attr.getType().equals("boolean") ) {
                            Boolean val = (Boolean)mbsc.getAttribute(objectName, attr.getName());
                            rr.set(oname + "`" + mname, (val)?1:0);
                        }
                    }
                    catch (Exception e) {}
                }
            }
            connector.close();

        }
        catch (Exception e) {
            rr.set("jezebel_status", e.getMessage());
        }
    }
}
