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
import java.util.Map;
import java.util.HashMap;
import java.sql.*;
import java.util.Properties;
import com.omniti.jezebel.check.JDBC;
import com.omniti.jezebel.JezebelCheck;
import java.lang.reflect.Method;
public class oracle extends JDBC implements JezebelCheck {
  static { try { Class.forName("oracle.jdbc.driver.OracleDriver"); }
           catch (Exception e) { throw new RuntimeException(e); } }
  protected String defaultPort() { return "1521"; }
  protected String jdbcConnectUrl(String host, String port, String db) {
    if(db.startsWith("/"))
      return "jdbc:oracle:thin:@//" + host + ":" + port + db;
    if(db.startsWith(":"))
      return "jdbc:oracle:thin:@" + host + ":" + port + db;
    // RAC connection description
    if(db.startsWith("("))
      return "jdbc:oracle:thin:@" + db;
    // Assume a SID (:name) as opposed to a SERVICE_NAME (/name)
    return "jdbc:oracle:thin:@" + host + ":" + port + ":" + db;
  }
  protected Map<String,String> setupBasicSSL() {
    HashMap<String,String> props = new HashMap<String,String>();
    props.put("oracle.net.ssl_cipher_suites", "(SSL_DH_anon_WITH_3DES_EDE_CBC_SHA, SSL_DH_anon_WITH_RC4_128_MD5,SSL_DH_anon_WITH_DES_CBC_SHA)");
    return props;
  }
  protected Connection jdbcConnection(String url, Properties props) throws SQLException {
    try {
      Class<?> odsc = Class.forName("oracle.jdbc.pool.OracleDataSource");
      Object ods = odsc.newInstance();

      Method m = odsc.getDeclaredMethod("setURL", String.class);
      m.invoke(ods, url);

      m = odsc.getDeclaredMethod("setConnectionCachingEnabled", boolean.class);
      m.invoke(ods, true);

      m = odsc.getDeclaredMethod("setFastConnectionFailoverEnabled", boolean.class);
      m.invoke(ods, true);

      m = odsc.getDeclaredMethod("getConnection", String.class, String.class, Properties.class);
      return (Connection)m.invoke(ods, props.getProperty("user"), props.getProperty("password"), props);
    }
    catch (Exception e) {
      return DriverManager.getConnection(url, props);
    }
  }
}
