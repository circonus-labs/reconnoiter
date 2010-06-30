package com.omniti.jezebel;

import java.util.Map;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

public class JezebelTools {
  static public String interpolate(String src,
                                   Map<String,String> attrs,
                                   Map<String,String> config) {
    Pattern p;
    Matcher m;
    String dst = src;

    p = Pattern.compile("%\\{([^\\}]+)\\}");
    while((m = p.matcher(dst)) != null && m.find()) {
      String token = dst.substring(m.start(1), m.end(1));
      dst = m.replaceFirst(config.get(token));
    }
    
    p = Pattern.compile("%\\[([^\\]]+)\\]");
    while((m = p.matcher(dst)) != null && m.find()) {
      String token = dst.substring(m.start(1), m.end(1));
      dst = m.replaceFirst(attrs.get(token));
    }

    return dst;
  }
}
