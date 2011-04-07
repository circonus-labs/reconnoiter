<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:ng="http://docbook.org/docbook-ng"
                xmlns:db="http://docbook.org/ns/docbook"
                xmlns:exsl="http://exslt.org/common"
                exclude-result-prefixes="exsl db ng"
                version='1.0'>

<!-- ********************************************************************
     $Id: stripns.xsl,v 1.2 2005/05/29 09:27:08 xmldoc Exp $
     ********************************************************************

     This file is part of the XSL DocBook Stylesheet distribution.
     See ../README or http://docbook.sf.net/release/xsl/current/ for
     copyright and other information.

     ******************************************************************** -->

<!-- Standalone stylesheet for stripping namespaces from DocBook 5/NG -->
<!-- docs. You currently need to do two-pass processing to use this: First, -->
<!-- transform your DocBook 5/NG source doc using this, then transform the -->
<!-- result as usual with the manpages/docbook.xsl stylesheet. Of course -->
<!-- you can always run the process using a pipe if you want. Example: -->

<!--   xsltproc ./manpages/stripns.xsl myRefEntry.xml \ -->
<!--     | xsltproc ./manpages/docbook.xsl - -->

<!-- It may be that there is actually some way to set it up as a -->
<!-- single-pass XSLT process, as is done with the HTML stylesheets. But -->
<!-- I've not yet figured out how to get that towork... -Mike -->

<!-- ==================================================================== -->

  <xsl:output method="xml"
              encoding="ISO-8859-1"
              indent="no"/>

  <xsl:template match="node() | @*">
    <xsl:copy>
      <xsl:apply-templates select="@* | node()"/>
    </xsl:copy>
  </xsl:template>

  <!-- ==================================================================== -->

  <xsl:template match="/">
    <xsl:choose>
      <xsl:when test="function-available('exsl:node-set')
                      and (*/self::ng:* or */self::db:*)">
        <!-- see comment in html/docbook.xsl -->
        <xsl:message>Stripping NS from DocBook 5/NG document.</xsl:message>
        <xsl:variable name="nons">
          <xsl:apply-templates mode="stripNS"/>
        </xsl:variable>
        <xsl:message>Processing stripped document.</xsl:message>
        <xsl:apply-templates select="exsl:node-set($nons)"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- ==================================================================== -->

  <xsl:template match="*" mode="stripNS">
    <xsl:choose>
      <xsl:when test="self::ng:* or self::db:*">
        <xsl:element name="{local-name(.)}">
          <xsl:copy-of select="@*"/>
          <xsl:apply-templates mode="stripNS"/>
        </xsl:element>
      </xsl:when>
      <xsl:otherwise>
        <xsl:copy>
          <xsl:copy-of select="@*"/>
          <xsl:apply-templates mode="stripNS"/>
        </xsl:copy>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="ng:link|db:link" mode="stripNS">
    <xsl:variable xmlns:xlink="http://www.w3.org/1999/xlink"
                  name="href" select="@xlink:href|@href"/>
    <xsl:choose>
      <xsl:when test="$href != '' and not(starts-with($href,'#'))">
        <ulink url="{$href}">
          <xsl:for-each select="@*">
            <xsl:if test="local-name(.) != 'href'">
              <xsl:copy/>
            </xsl:if>
          </xsl:for-each>
          <xsl:apply-templates mode="stripNS"/>
        </ulink>
      </xsl:when>
      <xsl:when test="$href != '' and starts-with($href,'#')">
        <link linkend="{substring-after($href,'#')}">
          <xsl:for-each select="@*">
            <xsl:if test="local-name(.) != 'href'">
              <xsl:copy/>
            </xsl:if>
          </xsl:for-each>
          <xsl:apply-templates mode="stripNS"/>
        </link>
      </xsl:when>
      <xsl:otherwise>
        <link>
          <xsl:copy-of select="@*"/>
          <xsl:apply-templates mode="stripNS"/>
        </link>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="comment()|processing-instruction()|text()" mode="stripNS">
    <xsl:copy/>
  </xsl:template>

</xsl:stylesheet>
