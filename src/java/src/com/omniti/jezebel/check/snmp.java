/*
 * Copyright (c) 2013, OmniTI Computer Consulting, Inc.
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
import java.util.ArrayList;
import java.util.Hashtable;
import java.util.Enumeration;
import java.util.Date;
import java.util.Map;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

import org.snmp4j.PDU;
import org.snmp4j.ScopedPDU;
import org.snmp4j.Snmp;
import org.snmp4j.Target;
import org.snmp4j.CommunityTarget;
import org.snmp4j.UserTarget;
import org.snmp4j.event.ResponseEvent;
import org.snmp4j.mp.MPv3;
import org.snmp4j.mp.SnmpConstants;
import org.snmp4j.security.AuthMD5;
import org.snmp4j.security.AuthSHA;
import org.snmp4j.security.PrivAES128;
import org.snmp4j.security.PrivAES192;
import org.snmp4j.security.PrivAES256;
import org.snmp4j.security.PrivDES;
import org.snmp4j.security.SecurityLevel;
import org.snmp4j.security.SecurityModels;
import org.snmp4j.security.SecurityProtocols;
import org.snmp4j.security.TSM;
import org.snmp4j.security.USM;
import org.snmp4j.security.UsmUser;
import org.snmp4j.smi.Address;
import org.snmp4j.smi.GenericAddress;
import org.snmp4j.smi.OctetString;
import org.snmp4j.smi.OID;
import org.snmp4j.smi.UdpAddress;
import org.snmp4j.smi.VariableBinding;
import org.snmp4j.transport.DefaultUdpTransportMapping;
import org.snmp4j.util.DefaultPDUFactory;
import org.snmp4j.util.TreeUtils;
import org.snmp4j.util.TreeEvent;


public class snmp implements JezebelCheck {
  public snmp() { }
  class oid_data {
    public String name;
    public String oid;
    public String metric_type;
  }
  public void perform(Map<String,String> check,
                      Map<String,String> config,
                      ResmonResult rr) {

    try{
      final String host                  = check.remove("target_ip");
      final String timeout               = check.remove("timeout");
      String port                        = config.remove("port");
      String version                     = config.remove("version");
      /* SNMP v1/v2c Specific */
      String community                   = config.remove("community");
      /* SNMP v3 Specific */
      final String security_level        = config.remove("security_level");
      final String auth_protocol         = config.remove("auth_protocol");
      final String privacy_protocol      = config.remove("privacy_protocol");
      final String auth_passphrase       = config.remove("auth_passphrase");
      final String privacy_passphrase    = config.remove("privacy_passphrase");
      final String security_engine       = config.remove("security_engine");
      final String context_engine        = config.remove("context_engine");
      final String security_name         = config.remove("security_name");
      final String context_name          = config.remove("context_name");
      final String walk_base             = config.remove("walk");
      final String separate_queries      = config.remove("separate_queries");
      /* Assigned Later */
      boolean send_separate_queries = false;
      final HashMap<String,String> oids  = new HashMap<String,String>();
      Target target;
      HashMap<String, ArrayList<oid_data>> oid_hashmap = new HashMap<String, ArrayList<oid_data>>();
      OctetString localEngineID = new OctetString(MPv3.createLocalEngineID());

      if (port == null) {
        port = "161";
      }
      if (version == null) {
        version = "2c";
      }
      if (community == null) {
        community = "public";
      }
      if ((separate_queries != null) && ((separate_queries.equals("on")) || separate_queries.equals("true"))) {
        send_separate_queries = true;
      }

      Address targetAddress = GenericAddress.parse("udp:"+host+"/"+port);

      for (Map.Entry<String, String> entry : config.entrySet())
      {
        String type = entry.getKey();
        if (type.length() > 4) {
          if (type.substring(0, 3).equals("oid")) {
            String type_string;
            String metric_type;
            oid_data data = new oid_data();
            data.name = type.substring(4);
            data.oid = entry.getValue();
            type_string = "type_" + data.name;
            data.metric_type = config.get(type_string);
            oids.put(data.name, data.oid);
            if (oid_hashmap.containsKey(data.oid.substring(1))) {
              ArrayList<oid_data> list = oid_hashmap.remove(data.oid.substring(1));
              list.add(data);
              oid_hashmap.put(data.oid.substring(1), list);
            }
            else {
              ArrayList<oid_data> list = new ArrayList<oid_data>();
              list.add(data);
              oid_hashmap.put(data.oid.substring(1), list);
            }
          }
        }
      }

      UdpAddress udpAddr = new UdpAddress(UdpAddress.ANY_IPADDRESS, 0);
      Snmp snmp = new Snmp(new DefaultUdpTransportMapping(udpAddr));
      /* the snmp connection is open... add a seprate try/catch so we can make sure we close it */
      try {
        if ((version.equals("1")) || (version.equals("2c"))) {
          target = getCommunityTarget(targetAddress, Integer.parseInt(timeout), community, version);
        }
        else {
            OID auth = null;
            OID priv = null;

            if (auth_protocol != null) {
              if (auth_protocol.equals("MD5")) {
                auth = AuthMD5.ID;
              }
              else if (auth_protocol.equals("SHA")) {
                auth = AuthSHA.ID;
              }
            }

            if (privacy_protocol != null) {
              if (privacy_protocol.equals("AES128")) {
                priv = PrivAES128.ID;
              }
              else if (privacy_protocol.equals("AES192")) {
                priv = PrivAES192.ID;
              }
              else if (privacy_protocol.equals("AES256")) {
                priv = PrivAES256.ID;
              }
              else if (privacy_protocol.equals("DES")) {
                priv = PrivDES.ID;
              }
            }

            SecurityProtocols sec = SecurityProtocols.getInstance();
            ((MPv3)snmp.getMessageProcessingModel(MPv3.ID)).setLocalEngineID(localEngineID.getValue());
            USM usm = new USM(sec, localEngineID, 0);
            SecurityModels.getInstance().addSecurityModel(usm);
            UsmUser user = new UsmUser(new OctetString(security_name),
                                           auth,
                                           new OctetString(auth_passphrase),
                                           priv,
                                           new OctetString(privacy_passphrase));
            snmp.getUSM().addUser(user);
            SecurityModels.getInstance().addSecurityModel(new TSM(localEngineID, false));
            target = getUserTarget(targetAddress, Integer.parseInt(timeout), security_level, security_name, security_engine);
        }
        snmp.listen();
        Iterator it = oids.entrySet().iterator();
        Exception ret = null;
        if (walk_base != null) {
          ret = walkHard(snmp, target, context_engine, context_name, walk_base, rr);
        }
        if (send_separate_queries == true) {
          ret = processIndividually(snmp, target, context_engine, context_name, it, oid_hashmap, rr);
        }
        else {
          ret = processAll(snmp, target, context_engine, context_name, it, oid_hashmap, rr);
        }
        if (ret != null) {
          throw(ret);
        }
        /* Go through remaining named values, return blank strings */
        blankMissingValues(oid_hashmap, rr);
      }
      catch (Exception e) {
        String error_msg = e.getMessage();
        if (error_msg != null) {
          rr.set("jezebel_status", error_msg);
        }
      }
      snmp.close();
    }
    catch(Exception e){
      String error_msg = e.getMessage();
      if (error_msg != null) {
        rr.set("jezebel_status", error_msg);
      }
    }
  }
  private Exception walkHard(Snmp snmp, Target target, String context_engine, String context_name,
                             String oid_string, ResmonResult rr) {
    try {
      OID walk_oid = new OID(oid_string);
      TreeUtils treeUtils = new TreeUtils(snmp, new DefaultPDUFactory());      
      List<TreeEvent> events = treeUtils.getSubtree(target, walk_oid);
      if(events == null || events.size() == 0) return null;
      for (TreeEvent event : events) {
        if(event != null){
          if (event.isError()) continue;
          VariableBinding[] varBindings = event.getVariableBindings();
          if(varBindings == null || varBindings.length == 0) continue;
          for (VariableBinding vb : varBindings) {
            OID oid = vb.getOid();
            String value = vb.toValueString();
            if (value != null && !value.equals("Null")) {
              coerceMetric(oid.toString(), vb.toValueString(), null, rr);
            }
          }
        }
      }
    }
    catch(Exception e) {
      return e;
    }
    return null;
  }
  private Exception processAll(Snmp snmp, Target target, String context_engine, String context_name, 
                               Iterator it, HashMap oid_hashmap, ResmonResult rr) {
    try {
      PDU request = createPDU(target, context_engine, context_name);
      PDU response = null;
      while (it.hasNext()) {
        Map.Entry pairs = (Map.Entry)it.next();
        /* We may have bad values... we want to just fail for this entry, not kick out of the
           check altogether */
        try {
          request.add(new VariableBinding(new OID(pairs.getValue().toString().substring(1))));
        }
        catch (Exception e) {
        }
      }
      ResponseEvent responseEvent = snmp.send(request, target);
      if (responseEvent != null) {
        response = responseEvent.getResponse();
        processResponse(response, oid_hashmap, rr);
      }
    }
    catch(Exception e) {
      return e;
    }
    return null;
  }
  private Exception processIndividually(Snmp snmp, Target target, String context_engine, String context_name,
                               Iterator it, HashMap oid_hashmap, ResmonResult rr) {
    try {
      while (it.hasNext()) {
        PDU request = createPDU(target, context_engine, context_name);
        PDU response = null;
        Map.Entry pairs = (Map.Entry)it.next();
        /* If we have a bad value, just continue, since each request can only contain one OID */
        try {
          request.add(new VariableBinding(new OID(pairs.getValue().toString().substring(1))));
        }
        catch (Exception e) {
          continue;
        }
        ResponseEvent responseEvent = snmp.send(request, target);
        if (responseEvent != null) {
          response = responseEvent.getResponse();
          processResponse(response, oid_hashmap, rr);
        }
      }
    }
    catch(Exception e) {
      return e;
    }
    return null;
  }
  private void blankMissingValues(HashMap oid_hashmap, ResmonResult rr) {
    Iterator entries = oid_hashmap.entrySet().iterator();
    while (entries.hasNext()) {
      Map.Entry thisEntry = (Map.Entry) entries.next();
      String key = (String)thisEntry.getKey();
      ArrayList list = (ArrayList)thisEntry.getValue();
      for (Object obj : list) {
        oid_data data = (oid_data)obj;
        coerceMetric(data.name, "", "string", rr);
      }
    }
  }
  private void processResponse(PDU response, HashMap oid_hashmap, ResmonResult rr) {
    if (response.size() < 1) {
      return;
    }
    VariableBinding vb = response.get(0);
    OID oid = vb.getOid();
    String error = checkForErrors(oid);
    if (error != null) {
      rr.set("error", error);
    }
    else {
      for (int i=0; i<response.size(); i++) {
        Object list;
        vb = response.get(i);
        oid =vb.getOid();
        list = oid_hashmap.remove(oid.toString());
        if (list != null) {
          for (Object obj : (ArrayList)list)
          {
            oid_data data = (oid_data)obj;
            String value = vb.toValueString();
            if (value != null && !value.equals("Null")) {
              coerceMetric(data.name, vb.toValueString(), data.metric_type, rr);
            }
          }
        }
      }
    }
  }
  private void coerceMetric(String name, String value, String coerceTo, ResmonResult rr) {
    try {
      if ((coerceTo == null) || (coerceTo.equals("guess"))) {
        rr.set(name, "0", value);
      }
      else if (coerceTo.equals("int32")) {
        rr.set(name, "i", value);
      }
      else if (coerceTo.equals("uint32")) {
        rr.set(name, "I", value);
      }
      else if (coerceTo.equals("int64")) {
        rr.set(name, "l", value);
      }
      else if (coerceTo.equals("uint64")) {
        rr.set(name, "L", value);
      }
      else if (coerceTo.equals("double")) {
        rr.set(name, "n", value);
      }
      else if (coerceTo.equals("string")) {
        rr.set(name, "s", value);
      }
      else {
        //Guess
        rr.set(name, "0", value);
      }
    }
    catch (Exception e) {
      //Guess
      rr.set(name, "0", value);
    }
  }
  private String checkForErrors(OID oid) {
    if (SnmpConstants.usmStatsUnsupportedSecLevels.equals(oid)) {
      return "Unsupported Security Level";
    }
    else if (SnmpConstants.usmStatsUnknownUserNames.equals(oid)) {
      return "Unknown Security Name";
    }
    else if (SnmpConstants.usmStatsUnknownEngineIDs.equals(oid)) {
      return "Unknown Engine ID";
    }
    else if (SnmpConstants.usmStatsWrongDigests.equals(oid)) {
      return "Wrong Digest";
    }
    else if (SnmpConstants.usmStatsDecryptionErrors.equals(oid)) {
      return "Decryption Error";
    }
    else if (SnmpConstants.snmpUnknownSecurityModels.equals(oid)) {
      return "Unknown Security Model";
    }
    else if (SnmpConstants.snmpInvalidMsgs.equals(oid)) {
      return "Invalid Message";
    }
    else if (SnmpConstants.snmpUnknownPDUHandlers.equals(oid)) {
      return "Unknown PDU Handler";
    }
    else if (SnmpConstants.snmpUnavailableContexts.equals(oid)) {
      return "Unavailable Context";
    }
    else if (SnmpConstants.snmpUnknownContexts.equals(oid)) {
      return "Unknown Context";
    }
    return null;
  }
  private Target getCommunityTarget (Address address, int timeout, String community, String version) {
    CommunityTarget target = new CommunityTarget();
    target.setCommunity(new OctetString(community));
    target.setAddress(address);
    target.setRetries(0);
    target.setTimeout(timeout/2);
    if (version.equals("1")) {
      target.setVersion(SnmpConstants.version1);
    }
    else {
      target.setVersion(SnmpConstants.version2c);
    }
    return target;
  }
  private Target getUserTarget (Address address, int timeout, String securityLevel, String securityName, String securityEngine) {
    UserTarget target = new UserTarget();
    if (securityLevel.equals("authPriv")) {
      target.setSecurityLevel(SecurityLevel.AUTH_PRIV);
    }
    else if (securityLevel.equals("authNoPriv")) {
      target.setSecurityLevel(SecurityLevel.AUTH_NOPRIV);
    }
    else {
      target.setSecurityLevel(SecurityLevel.NOAUTH_NOPRIV);
    }
    target.setSecurityName(new OctetString(securityName));
    if (securityEngine != null) {
      target.setAuthoritativeEngineID(OctetString.fromHexStringPairs(securityEngine).getValue());
    }

    target.setVersion(SnmpConstants.version3);
    target.setAddress(address);
    target.setRetries(0);
    target.setTimeout(timeout/2);
    return target;
  }
  private PDU createPDU(Target target, String contextEngine, String contextName) {
    PDU request;
    if (target.getVersion() == SnmpConstants.version3) {
      request = new ScopedPDU();
      ScopedPDU scopedPDU = (ScopedPDU)request;
      if (contextEngine != null) {
        scopedPDU.setContextEngineID(OctetString.fromHexStringPairs(contextEngine));
      }
      if (contextName != null) {
        scopedPDU.setContextName(new OctetString(contextName));
      }
    }
    else {
      request = new PDU();
    }
    request.setType(PDU.GET);
    return request;
  }
}

