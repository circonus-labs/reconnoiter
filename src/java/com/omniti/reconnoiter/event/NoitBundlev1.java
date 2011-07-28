/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * The software in this package is published under the terms of the GPL license
 * a copy of which can be found at:
 * https://labs.omniti.com/reconnoiter/trunk/src/java/LICENSE
 */

package com.omniti.reconnoiter.event;

import com.omniti.reconnoiter.event.NoitBundlev2;
import java.util.zip.Inflater;

public class NoitBundlev1 extends NoitBundlev2 {
  public String getPrefix() {
    return "B1";
  }

  protected byte[] decompress(byte input[], int len) {
    int olen = 0;
    Inflater decompresser = new Inflater();
    byte output[] = new byte[len];
    decompresser.setInput(input);
    try {
      olen = decompresser.inflate(output);
    } catch(java.util.zip.DataFormatException e) {
    }
    decompresser.end();
    if(len != olen) return new byte[0];
    return output;
  }

  public NoitBundlev1() {}
  public NoitBundlev1(String[] parts) throws Exception {
    super(parts);
  }
}
