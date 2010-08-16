import java.lang.System;
public class version {
  public static void main(String a[]) {
    Double v = Double.valueOf(System.getProperty("java.specification.version"));
    if(a.length > 0) {
      Double want = Double.valueOf(a[0]);
      if(v >= want) System.exit(0);
    }
    else {
      System.out.println(v);
    }
    System.exit(-1);
  }
}
