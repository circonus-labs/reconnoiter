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
  protected abstract String defaultPort();

  public void perform(Map<String,String> check,
                      Map<String,String> config,
                      ResmonResult rr) {
    String database = config.remove("database");
    String username = config.remove("user");
    String password = config.remove("password");
    String port = config.remove("port");
    if(port == null) port = defaultPort();
    String sql = config.remove("sql");
    String url = jdbcConnectUrl(check.get("target"), port, database);
    Properties props = new Properties();
    props.setProperty("user", username == null ? "" : username);
    props.setProperty("password", password == null ? "" : password);
    Set<Map.Entry<String,String>> set;
    set = config.entrySet();
    if(set != null) {
      Iterator<Map.Entry<String,String>> i = set.iterator();
      while(i.hasNext()) {
        Map.Entry<String,String> e = i.next();
        String key = e.getKey();
        if(key.startsWith("jdbc_")) {
          config.remove(key);
          props.setProperty(key.substring(5), e.getValue());
        }
      }
    }
    sql = JezebelTools.interpolate(sql, check, config);

    Connection conn = null;
    try {
      Date t1 = new Date();
      conn = DriverManager.getConnection(url, props);
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
    boolean auto = false;
    String autotype = config.get("autotype");
    if(autotype != null && autotype.equals("true")) auto = true;
    Statement st = null;
    ResultSet rs = null;
    try {
      st = conn.createStatement();
      rs = st.executeQuery(sql);
      while (rs.next()) {
        ResultSetMetaData rsmd = rs.getMetaData();
        int ncols = rsmd.getColumnCount();
  
        nrows++;
        if(ncols < 2) continue;
        String prefix = rs.getString(1);
        for(int i = 2; i <= ncols; i++) {
          String name = prefix;
          if(ncols > 2) name = name + '`' + rsmd.getColumnName(i);
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
                  try { Long l = Long.decode(s); rr.set(name, l); }
                  catch (NumberFormatException nfe) {
                    try { Double d = Double.valueOf(s); rr.set(name, d); }
                    catch (NumberFormatException nfe2) {
                      rr.set(name, s);
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
