package com.omniti.jezebel.check;
import com.omniti.jezebel.check.JDBC;
import com.omniti.jezebel.JezebelCheck;
public class mysql extends JDBC implements JezebelCheck {
  static { try { Class.forName("com.mysql.jdbc.Driver"); }
           catch (Exception e) { throw new RuntimeException(e); } }
  protected String defaultPort() { return "3306"; }
  protected String jdbcConnectUrl(String host, String port, String db) {
    return "jdbc:mysql://" + host + ":" + port + "/" + db;
  }
}
