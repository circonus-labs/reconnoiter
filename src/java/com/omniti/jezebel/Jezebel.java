package com.omniti.jezebel;

import org.mortbay.jetty.Handler;
import org.mortbay.jetty.handler.AbstractHandler;
import org.mortbay.jetty.Request;
import org.mortbay.jetty.Server;
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
    server = new Server(new Integer(port));
    Context root = new Context(server, "/", Context.SESSIONS);
    root.addServlet(new ServletHolder(new JezebelResmon()), "/resmon");
    root.addServlet(new ServletHolder(new JezebelDispatch()), "/dispatch/*");

    logger.info("Starting server on port " + port);
    try { server.start(); }
    catch (Exception e) { e.printStackTrace(); }
  }
}
