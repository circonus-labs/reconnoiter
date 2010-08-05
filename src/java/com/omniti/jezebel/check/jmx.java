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
