package com.omniti.jezebel;

import java.util.Map;
import com.omniti.jezebel.Resmon;

public interface JezebelCheck {
  void perform(Map<String,String> check,
               Map<String,String> config,
               Resmon r);
}
