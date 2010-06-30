package com.omniti.jezebel.check;
import com.omniti.jezebel.check.JDBC;
import com.omniti.jezebel.JezebelCheck;
public class oracle extends JDBC implements JezebelCheck {
  static { try { Class.forName("oracle.jdbc.driver.OracleDriver"); }
           catch (Exception e) { throw new RuntimeException(e); } }
  protected String defaultPort() { return "1521"; }
  protected String jdbcConnectUrl(String host, String port, String db) {
    return "jdbc:oracle:thin:@" + host + ":" + port + ":" + db;
  }
}
