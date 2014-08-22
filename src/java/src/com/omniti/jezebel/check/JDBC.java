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
import java.util.Set;
import java.util.Iterator;
import java.util.Properties;
import java.util.Date;
import java.sql.*;
import com.omniti.jezebel.ResmonResult;
import com.omniti.jezebel.JezebelCheck;
import com.omniti.jezebel.JezebelTools;

public abstract class JDBC implements JezebelCheck {
  public JDBC() { }
  protected abstract String jdbcConnectUrl(String host, String port, String db);
  protected abstract Connection jdbcConnection(String url, Properties props) throws SQLException;
  protected abstract String defaultPort();
  protected abstract Map<String,String> setupBasicSSL();

  public void perform(Map<String,String> check,
                      Map<String,String> config,
                      ResmonResult rr) {
    String dsn = config.remove("dsn");
    if (dsn != null) {
      String[] split = dsn.split("[ ]+");
      for (String s : split) {
        String[] kv = s.split("=");
        if (kv[0] != null && 2 == kv.length && kv[1] != null) {
          if (kv[0].equals("dbname")) {
            kv[0] = "database";
          }
          config.put(kv[0],kv[1]);
        }
      }
    }
    String database = config.remove("database");
    String username = config.remove("user");
    String password = config.remove("password");
    String port     = config.remove("port");
    if(port == null) port = defaultPort();

    String sql = config.remove("sql");
    String url = jdbcConnectUrl(check.get("target_ip"), port, database);

    Properties props = new Properties();
    props.setProperty("user", username == null ? "" : username);
    props.setProperty("password", password == null ? "" : password);
    if (config.containsKey("sslmode")) {
      String sslmode = config.remove("sslmode");
      if (sslmode != null && ! sslmode.equals("disable")) {
        Map<String,String> sslprops = setupBasicSSL();
        Set<Map.Entry<String,String>> set;
        set = sslprops.entrySet();
        if (set != null) {
          Iterator<Map.Entry<String,String>> i = set.iterator();
          while(i.hasNext()) {
            Map.Entry<String,String> e = i.next();
            props.setProperty(e.getKey(), e.getValue());
          }
        }
      }
    }
    Set<Map.Entry<String,String>> set;
    set = config.entrySet();
    if(set != null) {
      Iterator<Map.Entry<String,String>> i = set.iterator();
      while(i.hasNext()) {
        Map.Entry<String,String> e = i.next();
        String key = e.getKey();
        if(key.startsWith("jdbc_")) {
          props.setProperty(key.substring(5), e.getValue());
          config.remove(key);
        }
      }
    }

    // For MySQL and Postgres, make this act just like the C lib by default.
    // i.e. append the column name to the metric name (if not already set)
    if ( check.get("module").equals("mysql") || check.get("module").equals("postgres") ) {
      if ( ! config.containsKey("append_column_name") ) {
        config.put("append_column_name","true");
      }
    }

    // MySQL "show" queries need auto typing on or all you get is strings.
    if ( check.get("module").equals("mysql") ) {
      String sqllc = sql.toLowerCase();
      if ( sqllc.startsWith("show") ) {
        config.put("autotype", "true");
        config.put("mysql_show", "true");

        if ( sqllc.contains("slave status") || sqllc.contains("master status") ) {
          config.put("mysql_show_useonlycolname", "true");
        }
      }
    }

    sql = JezebelTools.interpolate(sql, check, config);

    Connection conn = null;
    try {
      Date t1 = new Date();
      conn = jdbcConnection(url, props);
      Date t2 = new Date();
      rr.set("connect_duration", t2.getTime() - t1.getTime());
      queryToResmon(conn, config, sql, rr);
      Date t3 = new Date();
      rr.set("query_duration", t3.getTime() - t2.getTime());
    }
    catch (SQLException e) { rr.set("jezebel_status", e.getMessage()); }
    finally {
      try { if(conn != null) conn.close(); }
      catch (SQLException e) { }
    }
  }
  protected void queryToResmon(Connection conn, Map<String,String> config,
                               String sql, ResmonResult rr) {
    int nrows = 0;
    boolean auto                      = false;
    boolean append                    = false;
    boolean show_query                = false;
    boolean show_useonlycolname       = false;

    String autotype                   = config.get("autotype");
    String append_column_name         = config.get("append_column_name");
    String mysql_show                 = config.get("mysql_show");
    String mysql_show_useonlycolname  = config.get("mysql_show_useonlycolname");

    if(autotype                   != null && autotype.equals("true"))                   auto = true;
    if(append_column_name         != null && append_column_name.equals("true"))         append = true;
    if(mysql_show                 != null && mysql_show.equals("true"))                 show_query = true;
    if(mysql_show_useonlycolname  != null && mysql_show_useonlycolname.equals("true"))  show_useonlycolname = true;

    Statement st = null;
    ResultSet rs = null;
    try {
      st = conn.createStatement();
      rs = st.executeQuery(sql);
      while (rs.next()) {
        ResultSetMetaData rsmd = rs.getMetaData();
        int ncols = rsmd.getColumnCount();
        int idx = (show_query && show_useonlycolname) ? 1 : 2;
  
        nrows++;
        if(ncols < 2) continue;
        String prefix = rs.getString(1);
        for(int i = idx; i <= ncols; i++) {
          String name     = prefix;
          String colname  = rsmd.getColumnName(i);

          // at some point (>5.1?) mysql started returning the colname as VARIABLE_VALUE
          // for various show * queries, to remain consistent with older versions and to mimic
          // what show actually shows to the user, convert this name to just Value.
          if (show_query && colname.equals("VARIABLE_VALUE")) colname = "Value";

          if(ncols > 2 || append) {
            if (show_query && show_useonlycolname) {
              name = colname;
            }
            else {
              name = name + '`' + colname;
            }
          }
          
          try {
            switch(rsmd.getColumnType(i)) {
              case Types.BOOLEAN:
                rr.set(name, rs.getBoolean(i) ? 1 : 0);
                break;
              case Types.TINYINT:
              case Types.SMALLINT:
              case Types.REAL:
              case Types.INTEGER:
              case Types.BIGINT:
                rr.set(name, rs.getLong(i));
                break;
              case Types.NUMERIC:
              case Types.FLOAT:
              case Types.DECIMAL:
              case Types.DOUBLE:
                rr.set(name, rs.getDouble(i));
                break;
              default:
                if(auto) {
                  String s = rs.getString(i);
                  if (s == null) {
                    // don't do any auto decoding if it is null, just treat
                    // it as a string since we have no way to guess
                    rr.set(name, s);
                  }
                  else {
                    try { Long l = Long.decode(s); rr.set(name, l); }
                    catch (NumberFormatException nfe) {
                      try { Double d = Double.valueOf(s); rr.set(name, d); }
                      catch (NumberFormatException nfe2) {
                        rr.set(name, s);
                      }
                    }
                  }
                } else {
                  rr.set(name, rs.getString(i));
                }
                break;
            }
          }
          catch (SQLException e) { rr.set("jezebel_status", e.getMessage()); }
        }
      }
    }
    catch (SQLException e) { rr.set("jezebel_status", e.getMessage()); }
    finally {
      try { rs.close(); } catch (Exception e) {}
      try { st.close(); } catch (Exception e) {}
    }
    rr.set("row_count", nrows);
  }
}
