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
import java.util.Hashtable;
import java.util.Map;
import java.util.Set;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import java.util.Iterator;
import java.util.concurrent.ConcurrentHashMap;

import javax.management.MBeanServerConnection;
import javax.management.MBeanInfo;
import javax.management.MBeanAttributeInfo;
import javax.management.ObjectName;
import javax.management.ObjectInstance;
import javax.management.remote.JMXConnector;
import javax.management.remote.JMXConnectorFactory;
import javax.management.remote.JMXServiceURL;
import javax.management.AttributeList;
import javax.management.Attribute;
import javax.management.openmbean.CompositeData;
import javax.management.openmbean.CompositeDataSupport;
import javax.management.openmbean.CompositeType;
import javax.management.openmbean.OpenType;
import javax.management.openmbean.SimpleType;
import javax.management.openmbean.TabularDataSupport;
import java.lang.management.MemoryUsage;

import com.omniti.jezebel.ResmonResult;
import com.omniti.jezebel.JezebelCheck;
import com.omniti.jezebel.Jezebel;

public class jmx implements JezebelCheck {
    private static ConcurrentHashMap<String, ConcurrentHashMap<ObjectName, ArrayList<String>>> attribute_cache = 
        new ConcurrentHashMap<String, ConcurrentHashMap<ObjectName, ArrayList<String>>>();

    public jmx() {}
    public void perform(Map<String,String> check,
                        Map<String,String> config,
                        ResmonResult rr) 
    {
        final String host = check.remove("target_ip");
        final String port = config.remove("port");
        final String username = config.remove("username");
        final String password = config.remove("password");
        final String mbean_domains = config.remove("mbean_domains");
	String uri = config.remove("uri");
	if (uri == null) {
	  uri = "/jmxrmi";
	}
	else {
	  if (!uri.substring(0, 1).equals("/")) {
            uri = "/" + uri;
	  }
	}
        final String jmxGlassFishConnectorString =
            "service:jmx:rmi:///jndi/rmi://" + host + ":" + port + uri;
        try {
            final JMXServiceURL jmxUrl =
                new JMXServiceURL(jmxGlassFishConnectorString);
            final Map<String,String[]> jmxEnv = new HashMap<String, String[]>();
            final String[] credentials = new String[] {username, password};
            jmxEnv.put( JMXConnector.CREDENTIALS, credentials );
            final JMXConnector connector =
                JMXConnectorFactory.connect(jmxUrl, jmxEnv);
            final MBeanServerConnection mbsc = connector.getMBeanServerConnection();

            // If the user supplied one or more mbean_properties_(.+) params, we will use them to filter
            // the mbeans we are able to pull, this is an OR, so any match lets the bean through.

            // Note that to start I'm assumg they will be something like mbean_properties_0, later on we
            // might want to allow the suffix to be a string, where that string would then be the attribute
            // or object name.
            ArrayList<HashMap<String,String>> filters = new ArrayList<HashMap<String,String>>();
            boolean has_filter = false;
            for ( Map.Entry<String, String> entry : config.entrySet() ) {
                if ( entry.getKey().startsWith("mbean_properties_") ) {
                    // The properties should come to us in the format: prop=value,prop2=value
                    // So convert each prop + value to a map and then put that on the arraylist
                    HashMap<String, String> filter = new HashMap<String, String>();
                    String[] kvs = entry.getValue().split(",");
                    for ( String kv : kvs ) {
                        String[] property = kv.split("=");
                        filter.put(property[0], property[1]);
                    }

                    filters.add(filter);
                    has_filter = true;
                }
            }


            rr.set("default_domain", mbsc.getDefaultDomain());
            rr.set("mbean_count", mbsc.getMBeanCount());

	    ConcurrentHashMap<ObjectName, ArrayList<String>> cache = null;
	    if ( ! attribute_cache.containsKey(check.get("name")) ) {
                Set<ObjectName> allObjectNames = mbsc.queryNames(null, null);

                ArrayList<String> domains = new ArrayList<String>();
                if ( mbean_domains != null ) {
                    domains = new ArrayList<String>(Arrays.asList(mbean_domains.split("\\s+")));
                }

                cache = new ConcurrentHashMap<ObjectName, ArrayList<String>>();

                for (ObjectName objectName : allObjectNames) {
                    if ( ! domains.isEmpty() && ! domains.contains(objectName.getDomain()) ) {
                        continue;
                    }

                    if ( has_filter ) {
                        Hashtable<String, String> property_list = objectName.getKeyPropertyList();
                        boolean passed_filters = false;
                        for ( HashMap<String, String> filter : filters ) {
                            boolean passes = true;
                            for ( Map.Entry<String, String> entry : filter.entrySet() ) {
                                if ( 
                                    ! property_list.containsKey(entry.getKey()) ||
                                    ! property_list.get(entry.getKey()).equals(entry.getValue()) 
                                ) {
                                    passes = false;
                                }
                            }

                            if ( passes ) {
                                passed_filters = true;
                                break; // as long as we pass one we are good to go
                            }
                        }

                        // If we didn't pass the filters, ignore this object
                        if ( ! passed_filters ) {
                            continue;
                        }
                    }

                    String oname = objectName.getDomain() + ":" + objectName.getCanonicalKeyPropertyListString();

                    MBeanInfo mbi = mbsc.getMBeanInfo(objectName);
                    MBeanAttributeInfo[] attribs = mbi.getAttributes();
                    ArrayList<String> attributes = new ArrayList<String>(attribs.length);

                    for (MBeanAttributeInfo attr : attribs) {
                        String type = attr.getType();

                        // Whitelist the types of attributes we will pull to only allow "Open MBeans" as defined here:
                        // http://www.oracle.com/technetwork/java/javase/tech/best-practices-jsp-136021.html#mozTocId817742
                        if (
                            type != null &&
                            (
                                    type.equals("int") 
                                ||  type.equals("long")
                                ||  type.equals("double")
                                ||  type.equals("boolean")
                                ||  type.equals("java.lang.String")
                                ||  type.equals("java.lang.Date")
                                ||  type.equals("java.lang.BigDecimal")
                                ||  type.equals("java.lang.BigInteger")
                                ||  type.equals("java.lang.Byte")
                                ||  type.equals("java.lang.Short")
                                ||  type.equals("java.lang.Integer")
                                ||  type.equals("java.lang.Long")
                                ||  type.equals("java.lang.Float")
                                ||  type.equals("java.lang.Double")
                                ||  type.equals("java.lang.Character")
                                ||  type.equals("java.lang.Boolean")
                                ||  type.equals("java.lang.Void")
                                ||  type.startsWith("javax.management.openmbean")
                            )
                        ) {
                            attributes.add(attr.getName());
                        }
                    }
                    cache.put(objectName, attributes);
                    getAttributeValues(mbsc, oname, objectName, attributes, rr);
                }

                // Don't cache anything for test checks, there is no current good way to identify these
                // though so this might change to be something other than the name
                if ( ! check.get("name").startsWith("c_test") ) {
                    attribute_cache.put(check.get("name"), cache);
                }
            }
            else {
                cache = attribute_cache.get(check.get("name"));
                for (ObjectName objectName : cache.keySet()) {
                    String oname = objectName.getDomain() + ":" + objectName.getCanonicalKeyPropertyListString();
                    getAttributeValues(mbsc, oname, objectName, cache.get(objectName), rr);
                }
            }
            connector.close();
        }
        catch (Exception e) {
            Jezebel.exceptionTraceLogger(e);
            rr.set("jezebel_status", e.getMessage());
        }
    }

    private void getAttributeValues (MBeanServerConnection mbsc, String oname, ObjectName objectName, ArrayList<String> attributes, ResmonResult rr) {
        try {
            AttributeList al = mbsc.getAttributes(objectName, attributes.toArray(new String[]{}));
            for (Attribute a : al.asList()) {
                getMetric(oname, a.getName(), a.getValue(), rr);
            }
        }
        catch (Exception e) {
            Jezebel.exceptionTraceLogger(e);
            rr.set("jezebel_status", e.getMessage());
        }
    }

    private void getMetric (String objectName, String metricName, Object value, ResmonResult rr) {
        try {
            if ( value == null ) return;

            if ( value instanceof String ) {
                String val = (String)value;
                if ( val != null && val.length() <= 256 ) {
                    rr.set(objectName + "`" + metricName, val);
                }
            }
            else if ( value instanceof Integer ) {
                rr.set(objectName + "`" + metricName, (Integer)value);
            }
            else if ( value instanceof Long ) {
                rr.set(objectName + "`" + metricName, (Long)value);
            }
            else if ( value instanceof Boolean ) {
                Boolean val = (Boolean)value;
                rr.set(objectName + "`" + metricName, (val)?1:0);
            }
            else if ( value instanceof Double ) {
                rr.set(objectName + "`" + metricName, (Double)value);
            }
            else if ( value instanceof Float ) {
                rr.set(objectName + "`" + metricName, (Float)value);
            }
            else if ( value instanceof CompositeDataSupport ) {
                CompositeDataSupport data = (CompositeDataSupport)value;
                CompositeType type = data.getCompositeType();
                Set keys = type.keySet();
                for ( Iterator it = keys.iterator(); it.hasNext(); ) {
                    String key = (String)it.next();
                    getMetric(objectName, metricName + "`" + key, data.get(key), rr);
                }
            }
            else if ( value instanceof TabularDataSupport ) {
                TabularDataSupport data = (TabularDataSupport)value;
                Set keys = data.keySet();
                for ( Iterator it = keys.iterator(); it.hasNext(); ) {
                    Object key = it.next();
                    for ( Iterator ki = ((List) key).iterator(); ki.hasNext(); ) {
                        Object key2 = ki.next();
                        CompositeData cd = data.get(new Object[] {key2});
                        getMetric(objectName, metricName + "`" + key2, cd.get("value"), rr);
                    }
                }
            }
            else if ( value.getClass().isArray() ) {
                String t = value.getClass().getName().substring(1,2);
                if ( t.equals("L") ) {
                    Object[] vals = (Object[])value;
                    for ( int i = 0, len = vals.length; i < len; ++i ) {
                        getMetric(objectName, metricName + "`" + i, vals[i], rr);
                    }
                }
                else {
                    ArrayList<Object> list = new ArrayList<Object>(0);
                    if ( t.equals("Z") ) {
                        boolean[] vals = (boolean[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( boolean v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("B") ) {
                        byte[] vals = (byte[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( byte v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("C") ) {
                        char[] vals = (char[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( char v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("D") ) {
                        double[] vals = (double[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( double v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("F") ) {
                        float[] vals = (float[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( float v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("I") ) { 
                        int[] vals = (int[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( int v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("J") ) {
                        long[] vals = (long[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( long v : vals ) { list.add(v); }
                    }
                    else if ( t.equals("S") ) {
                        short[] vals = (short[])value;
                        list = new ArrayList<Object>(vals.length);
                        for ( short v : vals ) { list.add(v); }
                    }

                    for ( int i = 0, len = list.size(); i < len; ++i ) {
                        getMetric(objectName, metricName + "`" + i, list.get(i), rr);
                    }
                }
            }
            else {
                Jezebel.log("Error, could not handle. name: " + objectName+"`"+metricName + " type: " + value.getClass().getName(), "error");
            }
        }
        catch (Exception e) { 
            Jezebel.log("Exception: " + objectName+"`"+metricName, "error");
            Jezebel.exceptionTraceLogger(e);
        }
    }
}
