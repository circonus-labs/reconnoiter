package com.omniti.jezebel.check;
import java.util.Map;
import java.util.Set;
import java.util.HashMap;
import java.sql.*;
import java.util.Properties;
import java.util.Date;
import com.omniti.jezebel.check.JDBC;
import com.omniti.jezebel.JezebelCheck;
import com.omniti.jezebel.ResmonResult;
import com.omniti.jezebel.Jezebel;
import com.mongodb.*;
import org.bson.*;
public class mongodb extends JDBC implements JezebelCheck {
//  static { try { Class.forName("mongodb.jdbc.MongoDriver"); }
//           catch (Exception e) { throw new RuntimeException(e); } }

  protected String defaultPort() { return "27017"; }

  protected String jdbcConnectUrl(String host, String port, String db, Properties props) {
    if (props.getProperty("user") != null && props.getProperty("password") != null) {
      return "mongodb://" + props.getProperty("user") + ":" + props.getProperty("password") + "@" + host + ":" + port + "/" + db;
    }

    return "mongodb://" + host + ":" + port + "/" + db;
  }

  // This is all taken care of internally by the driver (i think?)
  protected Map<String,String> setupBasicSSL() {
    HashMap<String,String> props = new HashMap<String,String>();
    return props;
  }

  protected Connection jdbcConnection(String url, Properties props) throws SQLException {
    return DriverManager.getConnection(url, props);
  }

  static protected void queryToResmon(String url, Map<String,String> config, String query, ResmonResult rr) {
    MongoClientURI uri  = new MongoClientURI(url);
    MongoClient client = new MongoClient(uri);
    DB db = client.getDB(uri.getDatabase());
    CommandResult cr = db.command(query);

    if (!cr.ok()) {
      rr.set("_query_status", "notok");
      rr.set("_query_error", cr.getErrorMessage());
    }
    else {
      rr.set("_query_status", "ok");

      translateEntrySet(cr.entrySet(), rr, "");
    }
  }

  static void translateEntrySet(Set<Map.Entry<String, Object>> set, ResmonResult rr, String prefix) {
    for (Map.Entry<String, Object> entry : set) {
      String name = prefix + entry.getKey();

      if (entry.getValue() instanceof Integer) {
        rr.set(name, (Integer)entry.getValue());
      }
      else if(entry.getValue() instanceof Long) {
        rr.set(name, (Long)entry.getValue());
      }
      else if (entry.getValue() instanceof Double) {
        rr.set(name, (Double)entry.getValue());
      }
      else if (
          entry.getValue() instanceof String ||
          entry.getValue() instanceof java.util.Date ||
          entry.getValue() instanceof Boolean
      ) {
        rr.set(name, entry.getValue().toString());
      }
      else if (entry.getValue() instanceof com.mongodb.BasicDBObject) {
        translateEntrySet(((BasicDBObject)entry.getValue()).entrySet(), rr, name+"`");
      }
      else if (entry.getValue() instanceof com.mongodb.BasicDBList) {
        int size = ((BasicDBList)entry.getValue()).size();
        rr.set(name, String.join(",", ((BasicDBList)entry.getValue()).toArray(new String[size])));
      }
      else {
        rr.set(name, entry.getValue().getClass().getName());
      }
    }
  }
}
