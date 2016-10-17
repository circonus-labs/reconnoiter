package com.omniti.jezebel.check;
import java.util.Map;
import java.util.Set;
import java.util.HashMap;
import java.util.Properties;
import java.util.Date;
import com.omniti.jezebel.JezebelCheck;
import com.omniti.jezebel.ResmonResult;
import com.omniti.jezebel.Jezebel;
import com.mongodb.*;
import org.bson.*;
public class mongodb implements JezebelCheck {
  public mongodb() {}

  public void perform(Map<String,String> check,
                        Map<String,String> config,
                        ResmonResult rr)
  {
    final String user   = config.remove("username");
    final String pass   = config.remove("password");
    final String host   = check.remove("target_ip");
    final String port   = config.remove("port");
    final String dbname = config.remove("dbname");
    String url          = "mongodb://" + host + ":" + port + "/" + dbname;

    if (user != null && pass != null) {
      url = "mongodb://" + user + ":" + pass + "@" + host + ":" + port + "/" + dbname;
    }

    try {
      Date t1 = new Date();
      MongoClientURI uri  = new MongoClientURI(url);
      MongoClient client = new MongoClient(uri);
      DB db = client.getDB(uri.getDatabase());
      Date t2 = new Date();
      rr.set("connect_duration", t2.getTime() - t1.getTime());

      if (config.containsKey("command")) {
        CommandResult cr = db.command(config.remove("command"));
        Date t3 = new Date();
        rr.set("query_duration", t3.getTime() - t2.getTime());

        if (!cr.ok()) {
          rr.set("query_status", "notok");
          rr.set("query_error", cr.getErrorMessage());
        }
        else {
          rr.set("query_status", "ok");
          entrySetToResmon(cr.entrySet(), rr, "");
        }
      }
      else {
        rr.set("jezebel_status", "no command provided in check");
      }
    }
    catch (Exception e) {
      rr.set("jezebel_status", e.getMessage());
    }
  }

  static void entrySetToResmon(Set<Map.Entry<String, Object>> set, ResmonResult rr, String prefix) {
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
          entry.getValue() instanceof Boolean ||
          entry.getValue() instanceof org.bson.types.ObjectId
      ) {
        rr.set(name, entry.getValue().toString());
      }
      else if (entry.getValue() instanceof org.bson.types.BSONTimestamp) {
        int time = ((org.bson.types.BSONTimestamp)entry.getValue()).getTime();
        rr.set(name, time);
        rr.set(name+"_elapsed", System.currentTimeMillis()/1000 - time);
      }
      else if (entry.getValue() instanceof com.mongodb.BasicDBObject) {
        entrySetToResmon(((BasicDBObject)entry.getValue()).entrySet(), rr, name+"`");
      }
      else if (entry.getValue() instanceof com.mongodb.BasicDBList) {
        int idx = 0;
        for (Object lo : (BasicDBList)entry.getValue()) {
            if (lo instanceof com.mongodb.BasicDBObject) {
                entrySetToResmon(((BasicDBObject)lo).entrySet(), rr, name+"`"+idx+"`");
            }
            else {
                Map<String, Object> m = new HashMap<String, Object>();
                m.put(String.valueOf(idx), lo);

                entrySetToResmon(m.entrySet(), rr, name+"`");
            }
            idx++;
        }
      }
      else {
        rr.set(name, entry.getValue().getClass().getName());
      }
    }
  }
}
