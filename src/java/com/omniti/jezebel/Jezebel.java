/*
 * Copyright (c) 2010, OmniTI Computer Consulting, Inc.
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

package com.omniti.jezebel;

import org.mortbay.jetty.Handler;
import org.mortbay.jetty.handler.AbstractHandler;
import org.mortbay.jetty.Request;
import org.mortbay.jetty.Server;
import org.mortbay.jetty.bio.SocketConnector;
import org.mortbay.jetty.servlet.Context;
import org.mortbay.jetty.servlet.ServletHolder;
import org.apache.log4j.BasicConfigurator;
import org.apache.log4j.Logger;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.CommandLineParser;
import org.apache.commons.cli.ParseException;
import org.apache.commons.cli.PosixParser;
import com.omniti.jezebel.JezebelResmon;
import com.omniti.jezebel.JezebelDispatch;

public class Jezebel {
  static Logger logger = Logger.getLogger(Jezebel.class.getName());

  static final public void main(String args[]) {
    String port;
    Server server;
    SocketConnector connector;
    CommandLine cmd = null;
    CommandLineParser parser = new PosixParser();
    Options o = new Options();

    BasicConfigurator.configure();

    o.addOption("p", true, "port");
    try {
      cmd = parser.parse(o, args);
    }
    catch (ParseException exp ) {
      System.err.println( "Parsing failed.  Reason: " + exp.getMessage() );
      System.exit(-1);
    }

    port = cmd.getOptionValue("p");
    if(port == null) port = "8083";
    connector = new SocketConnector();
    connector.setPort(new Integer(port));
    connector.setHost("127.0.0.1");
    server = new Server();
    server.addConnector(connector);
    Context root = new Context(server, "/", Context.SESSIONS);
    root.addServlet(new ServletHolder(new JezebelResmon()), "/resmon");
    root.addServlet(new ServletHolder(new JezebelDispatch()), "/dispatch/*");

    logger.info("Starting server on port " + port);
    try { server.start(); }
    catch (Exception e) { e.printStackTrace(); }
  }
}
