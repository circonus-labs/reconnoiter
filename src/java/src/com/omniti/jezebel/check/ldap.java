/*
 * Copyright (c) 2012, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package com.omniti.jezebel.check;

import com.omniti.jezebel.ResmonResult;
import com.omniti.jezebel.JezebelCheck;
import com.omniti.jezebel.Jezebel;

import javax.naming.*;
import javax.naming.directory.*;
import java.util.Hashtable;
import java.util.Enumeration;
import java.util.Date;
import java.util.Map;

public class ldap implements JezebelCheck {
  public ldap() { }
  public void perform(Map<String,String> check,
                      Map<String,String> config,
                      ResmonResult rr) {

    try{
      final String host     = check.remove("target_ip");
      final String port     = config.remove("port");
      final String DN       = config.remove("dn");
      final String authtype = config.remove("authtype");
      final String user     = config.remove("security_principal");
      final String password = config.remove("password");

      Hashtable<String, Object> env = new Hashtable<String, Object>();
      Hashtable<String, Integer> counts = new Hashtable<String, Integer>();
      long startTime, endTime, diff;
      int count = 0;

      String URL = "ldap://" + host;

      if (port != null) {
        URL = URL + ":" + port;
      }
      else {
        URL = URL + ":389";
      }

      env.put("com.sun.jndi.ldap.connect.timeout", "5000");
      env.put("com.sun.jndi.ldap.read.timeout", "5000");
      env.put(Context.INITIAL_CONTEXT_FACTORY,"com.sun.jndi.ldap.LdapCtxFactory");
      env.put(Context.PROVIDER_URL, URL);

      if (authtype == null) {
        env.put(Context.SECURITY_AUTHENTICATION, "none");
      }
      else {
        env.put(Context.SECURITY_AUTHENTICATION, authtype);
      }

      if (user != null) {
          env.put(Context.SECURITY_PRINCIPAL, user);
      }

      if (password != null) {
        env.put(Context.SECURITY_CREDENTIALS, password);
      }

      startTime = new Date().getTime();
      Context ctx = new InitialContext(env);
      NamingEnumeration list = ctx.list(DN);
      endTime = new Date().getTime();
      diff = endTime - startTime;

      rr.set("time_to_connect_ms", diff);

      while (list.hasMore()) {
        NameClassPair nc = (NameClassPair)list.next();
        String name = nc.getName();
        String[] splitString = name.split("=", 2);
        if (splitString.length == 2)
        {
          String type = splitString[0];
          String value = splitString[1];
          if (counts.containsKey(type))
          {
            counts.put(type, counts.get(type) + 1);
          }
          else
          {
            counts.put(type, 1);
          }
          count++;
        }
      }
      ctx.close();
      Enumeration keys = counts.keys();
      while( keys.hasMoreElements() )
      {
        String key = (String)keys.nextElement();
        Object value = counts.get(key);
        String metName = key + "_object_count";
        rr.set(metName, (Integer)value);
      }
      rr.set("total_objects", count);
    }
    catch(Exception e){
      rr.set("jezebel_status", e.getMessage());
    }
  }
}

