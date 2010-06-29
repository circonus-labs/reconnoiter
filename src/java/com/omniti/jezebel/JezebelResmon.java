package com.omniti.jezebel;

import org.mortbay.jetty.Handler;
import org.mortbay.jetty.handler.AbstractHandler;
import org.mortbay.jetty.Request;
import org.mortbay.jetty.Server;
import org.mortbay.jetty.servlet.Context;
import org.mortbay.jetty.servlet.ServletHolder;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;
import javax.servlet.ServletException;
import java.io.IOException;
import java.util.Hashtable;
import javax.servlet.*;
import javax.servlet.http.*;

import com.omniti.jezebel.Resmon;
import com.omniti.jezebel.ResmonResult;


public class JezebelResmon extends HttpServlet {
    private static Resmon r = new Resmon();
    private static Hashtable<String,ResmonResult> mods = new Hashtable<String,ResmonResult>();

    public JezebelResmon() { }
    public void doGet(HttpServletRequest request,
                      HttpServletResponse response) {
        r.write(response);
    }

    static public ResmonResult getModule(String modname) {
        ResmonResult mod;
        mod = mods.get(modname);
        if(mod == null) {
            mod = r.addResult("jezebel", modname);
            mods.put(modname, mod);
        }
        return mod;
    }
}
