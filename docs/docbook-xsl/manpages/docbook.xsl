<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:exsl="http://exslt.org/common"
                exclude-result-prefixes="exsl"
                version='1.0'>

  <xsl:import href="../html/docbook.xsl"/>

  <xsl:output method="text"
              encoding="UTF-8"
              indent="no"/>

<!-- ********************************************************************
     $Id: docbook.xsl,v 1.45 2005/07/11 10:29:58 xmldoc Exp $
     ********************************************************************

     This file is part of the XSL DocBook Stylesheet distribution.
     See ../README or http://docbook.sf.net/release/xsl/current/ for
     copyright and other information.

     ******************************************************************** -->

<!-- ==================================================================== -->

  <xsl:include href="../common/refentry.xsl"/>
  <xsl:include href="param.xsl"/>
  <xsl:include href="utility.xsl"/>
  <xsl:include href="info.xsl"/>
  <xsl:include href="other.xsl"/>
  <xsl:include href="refentry.xsl"/>
  <xsl:include href="block.xsl"/>
  <xsl:include href="inline.xsl"/>
  <xsl:include href="synop.xsl"/>
  <xsl:include href="lists.xsl"/>
  <xsl:include href="links.xsl"/>

<!-- ==================================================================== -->

  <!-- * if document does not contain at least one refentry, then emit a -->
  <!-- * message and stop -->
  <xsl:template match="/">
    <xsl:choose>
      <xsl:when test="//refentry">
        <xsl:apply-templates select="//refentry"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:message>No refentry elements!</xsl:message>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <!-- ============================================================== -->

  <xsl:template match="refentry">

    <!-- * Just use the first refname found as the "name" of the man -->
    <!-- * page (which may different from the "title"...) -->
    <xsl:variable name="first.refname" select="refnamediv[1]/refname[1]"/>

    <!-- * Because there are several times when we need to check *info of -->
    <!-- * each refentry and *info of its parent, we get those and store -->
    <!-- * as node-sets in memory. -->

    <!-- * Make a node-set with contents of *info -->
    <xsl:variable name="get.info" select="(info|refentryinfo|docinfo)[1]"/>
    <xsl:variable name="info" select="exsl:node-set($get.info)"/>
    <!-- * Make a node-set with contents of parent's *info -->
    <xsl:variable name="get.parentinfo"
                  select="(../info
                          |../referenceinfo
                          |../articleinfo
                          |../sectioninfo
                          |../appendixinfo
                          |../chapterinfo
                          |../sect1info
                          |../sect2info
                          |../sect3info
                          |../sect4info
                          |../sect5info
                          |../partinfo
                          |../prefaceinfo
                          |../docinfo)[1]"/>
    <xsl:variable name="parentinfo" select="exsl:node-set($get.parentinfo)"/>

    <!-- * The get.refentry.metadata template is in -->
    <!-- * ../common/refentry.xsl. It looks for metadata in $info -->
    <!-- * and/or $parentinfo and in various other places and -->
    <!-- * then puts it into a form that's easier for us to digest. -->
    <xsl:variable name="get.refentry.metadata">
      <xsl:call-template name="get.refentry.metadata">
        <xsl:with-param name="refname" select="$first.refname"/>
        <xsl:with-param name="info" select="$info"/>
        <xsl:with-param name="parentinfo" select="$parentinfo"/>
        <xsl:with-param name="prefs" select="$refentry.metadata.prefs"/>
      </xsl:call-template>
    </xsl:variable>
    <xsl:variable name="refentry.metadata" select="exsl:node-set($get.refentry.metadata)"/>

    <!-- * Assemble the various parts into a complete page, then store into -->
    <!-- * $manpage.contents so that we can manipluate them further. -->
    <xsl:variable name="manpage.contents">
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <!-- * top.comment = commented-out section at top of roff source -->
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <xsl:call-template name="top.comment"/>
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <!-- * TH.title.line = title line in header/footer of man page -->
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <xsl:call-template name="TH.title.line">
        <!-- * .TH TITLE  section  extra1  extra2  extra3 -->
        <!-- *  -->
        <!-- * According to the man(7) man page: -->
        <!-- *  -->
        <!-- * extra1 = date,   "the date of the last revision" -->
        <!-- * extra2 = source, "the source of the command" -->
        <!-- * extra3 = manual, "the title of the manual -->
        <!-- *                  (e.g., Linux Programmer's Manual)" -->
        <!-- * -->
        <!-- * So, we end up with: -->
        <!-- *  -->
        <!-- * .TH TITLE  section  date  source  manual -->
        <!-- * -->
        <xsl:with-param name="title"   select="$refentry.metadata/title"/>
        <xsl:with-param name="section" select="$refentry.metadata/section"/>
        <xsl:with-param name="extra1"  select="$refentry.metadata/date"/>
        <xsl:with-param name="extra2"  select="$refentry.metadata/source"/>
        <xsl:with-param name="extra3"  select="$refentry.metadata/manual"/>
      </xsl:call-template>
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <!-- * Set default hyphenation, justification, and line-breaking -->
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <xsl:call-template name="set.default.formatting"/>
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <!-- * Main body of man page -->
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <xsl:apply-templates/>
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <!-- * AUTHOR section -->
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <xsl:call-template name="author.section">
        <xsl:with-param name="info" select="$info"/>
        <xsl:with-param name="parentinfo" select="$parentinfo"/>
      </xsl:call-template>
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <!-- * LINKS list (only if user wants links numbered and/or listed) -->
      <!-- * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->
      <xsl:if test="$man.links.list.enabled != 0 or
                    $man.links.are.numbered != 0">
        <xsl:call-template name="links.list"/>
      </xsl:if>
    </xsl:variable> <!-- * end of manpage.contents -->

    <!-- * Prepare the page contents for final output, then store in -->
    <!-- * $manpage.contents.prepared so the we can pass it on to the -->
    <!-- * write.text.chunk() function -->
    <xsl:variable name="manpage.contents.prepared">
      <!-- * "Preparing" the page contents involves, at a minimum, -->
      <!-- * doubling any backslashes found (so they aren't interpreted -->
      <!-- * as roff escapes). -->
      <!-- * -->
      <!-- * If $charmap.enabled is true, "preparing" the page contents also -->
      <!-- * involves applying a character map to convert Unicode symbols and -->
      <!-- * special characters into corresponding roff escape sequences. -->
      <xsl:call-template name="prepare.manpage.contents">
        <xsl:with-param name="content" select="$manpage.contents"/>
      </xsl:call-template>
    </xsl:variable>
    
    <!-- * Write the prepared page contents to disk to create -->
    <!-- * the final man page. -->
    <xsl:call-template name="write.man.file">
      <xsl:with-param name="name" select="$first.refname"/>
      <xsl:with-param name="section" select="$refentry.metadata/section"/>
      <xsl:with-param name="content" select="$manpage.contents.prepared"/>
    </xsl:call-template>

    <!-- * Generate "stub" (alias) pages (if any needed) -->
    <xsl:call-template name="write.stubs">
      <xsl:with-param name="first.refname" select="$first.refname"/>
      <xsl:with-param name="section" select="$refentry.metadata/section"/>
    </xsl:call-template>

  </xsl:template>

</xsl:stylesheet>
