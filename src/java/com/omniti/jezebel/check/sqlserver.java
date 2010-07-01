package com.omniti.jezebel.check;
import com.omniti.jezebel.check.JDBC;
import com.omniti.jezebel.JezebelCheck;
public class sqlserver extends JDBC implements JezebelCheck {
  static { try { Class.forName("com.microsoft.sqlserver.jdbc.SQLServerDriver"); }
           catch (Exception e) { throw new RuntimeException(e); } }
  protected String defaultPort() { return "1433"; }
  protected String jdbcConnectUrl(String host, String port, String db) {
    return "jdbc:sqlserver://" + host + ":" + port + ";databaseName=" + db;
  }
}
