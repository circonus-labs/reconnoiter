package com.omniti.jezebel;

import com.omniti.jezebel.JezebelCheck;
import java.util.Map;

public class SampleCheck implements JezebelCheck {
  public SampleCheck() { }
  public void perform(Map<String,String> info,
                      Map<String,String> config,
                      ResmonResult rr) {
    rr.set("mood", "happy");
    rr.set("x", 1234);
    rr.set("y", 12.342354);
  }
}
