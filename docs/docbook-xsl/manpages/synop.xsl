<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:exsl="http://exslt.org/common"
                exclude-result-prefixes="exsl"
                version='1.0'>

<!-- ********************************************************************
     $Id: synop.xsl,v 1.21 2005/07/13 03:48:44 xmldoc Exp $
     ********************************************************************

     This file is part of the XSL DocBook Stylesheet distribution.
     See ../README or http://docbook.sf.net/release/xsl/current/ for
     copyright and other information.

     ******************************************************************** -->

<!-- * Note: If you are looking for the <synopsis> element, you won't -->
<!-- * find any code here for handling it. It is a _verbatim_ -->
<!-- * environment; check the block.xsl file instead. -->

<xsl:template match="synopfragment">
  <xsl:text>.PP&#10;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="group|arg">
  <xsl:variable name="choice" select="@choice"/>
  <xsl:variable name="rep" select="@rep"/>
  <xsl:variable name="sepchar">
    <xsl:choose>
      <xsl:when test="ancestor-or-self::*/@sepchar">
        <xsl:value-of select="ancestor-or-self::*/@sepchar"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text> </xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:if test="position()>1"><xsl:value-of select="$sepchar"/></xsl:if>
  <xsl:choose>
    <xsl:when test="$choice='plain'">
      <!-- * do nothing -->
    </xsl:when>
    <xsl:when test="$choice='req'">
      <xsl:value-of select="$arg.choice.req.open.str"/>
    </xsl:when>
    <xsl:when test="$choice='opt'">
      <xsl:value-of select="$arg.choice.opt.open.str"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$arg.choice.def.open.str"/>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:variable name="arg">
    <xsl:apply-templates/>
  </xsl:variable>
  <xsl:choose>
    <xsl:when test="local-name(.) = 'arg' and not(ancestor::arg)">
      <!-- * Prevent arg contents from getting wrapped and broken up -->
      <xsl:variable name="arg.wrapper">
        <Arg><xsl:value-of select="normalize-space($arg)"/></Arg>
      </xsl:variable>
      <xsl:apply-templates mode="prevent.line.breaking"
                           select="exsl:node-set($arg.wrapper)"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$arg"/>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:choose>
    <xsl:when test="$rep='repeat'">
      <xsl:value-of select="$arg.rep.repeat.str"/>
    </xsl:when>
    <xsl:when test="$rep='norepeat'">
      <xsl:value-of select="$arg.rep.norepeat.str"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$arg.rep.def.str"/>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:choose>
    <xsl:when test="$choice='plain'">
      <xsl:if test='arg'>
      <xsl:value-of select="$arg.choice.plain.close.str"/>
      </xsl:if>
    </xsl:when>
    <xsl:when test="$choice='req'">
      <xsl:value-of select="$arg.choice.req.close.str"/>
    </xsl:when>
    <xsl:when test="$choice='opt'">
      <xsl:value-of select="$arg.choice.opt.close.str"/>
    </xsl:when>
    <xsl:otherwise>
      <xsl:value-of select="$arg.choice.def.close.str"/>
    </xsl:otherwise>
  </xsl:choose>
</xsl:template>

<xsl:template match="command">
  <xsl:call-template name="suppress.hyphenation"/>
  <xsl:apply-templates mode="bold" select="."/>
</xsl:template>

<xsl:template match="function[not(ancestor::command)]">
  <xsl:call-template name="suppress.hyphenation"/>
  <xsl:apply-templates mode="bold" select="."/>
</xsl:template>

<xsl:template match="parameter[not(ancestor::command)]">
  <xsl:call-template name="suppress.hyphenation"/>
  <xsl:apply-templates mode="italic" select="."/>
</xsl:template>

<xsl:template match="sbr">
  <xsl:text>&#10;</xsl:text>
  <xsl:text>.br&#10;</xsl:text>
</xsl:template>

<xsl:template match="cmdsynopsis">
  <!-- * if justification is enabled by default, turn it off temporarily -->
  <xsl:if test="$man.justify != 0">
    <xsl:text>.ad l&#10;</xsl:text>
  </xsl:if>
  <!-- * if hyphenation is enabled by default, turn it off temporarily -->
  <xsl:if test="$man.hyphenate != 0">
    <xsl:text>.hy 0&#10;</xsl:text>
  </xsl:if>
  <xsl:text>.HP </xsl:text>
  <xsl:value-of select="string-length (normalize-space (command)) + 1"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>&#10;</xsl:text>
  <!-- * if justification is enabled by default, turn it back on -->
  <xsl:if test="$man.justify != 0">
    <xsl:text>.ad&#10;</xsl:text>
  </xsl:if>
  <!-- * if hyphenation is enabled by default, turn it back on -->
  <xsl:if test="$man.hyphenate != 0">
    <xsl:text>.hy&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<!-- ==================================================================== -->

<!-- * Within funcsynopis output, disable hyphenation, and use -->
<!-- * left-aligned filling for the duration of the synopsis, so that -->
<!-- * line breaks only occur between separate paramdefs. -->
<xsl:template match="funcsynopsis">
  <!-- * if justification is enabled by default, turn it off temporarily -->
  <xsl:if test="$man.justify != 0">
    <xsl:text>.ad l&#10;</xsl:text>
  </xsl:if>
  <!-- * if hyphenation is enabled by default, turn it off temporarily -->
  <xsl:if test="$man.hyphenate != 0">
    <xsl:text>.hy 0&#10;</xsl:text>
  </xsl:if>
  <xsl:apply-templates/>
  <!-- * if justification is enabled by default, turn it back on -->
  <xsl:if test="$man.justify != 0">
    <xsl:text>.ad&#10;</xsl:text>
  </xsl:if>
  <!-- * if hyphenation is enabled by default, turn it back on -->
  <xsl:if test="$man.hyphenate != 0">
    <xsl:text>.hy&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<!-- * NOTE TO DEVELOPERS: Below you will find many "bold" calls. -->
<!-- * -->
<!-- * The reason is that we need to bold each bit of Funcsynopsis -->
<!-- * separately, to get around the limitations of not being able -->
<!-- * to do \fBfoo \fBbar\fI baz\fR and have "baz" get bolded. -->
<!-- * -->
<!-- * And the reason we need to bold so much stuff is that the -->
<!-- * man(7) man page says this: -->
<!-- * -->
<!-- *   For functions, the arguments are always specified using -->
<!-- *   italics, even in the SYNOPSIS section, where the rest of -->
<!-- *   the function is specified in bold: -->
<!-- * -->
<!-- * And if you take a look through the contents of the man/man2 -->
<!-- * directory on your system, you'll see that most existing pages -->
<!-- * do follow this "bold everything in function synopsis " rule. -->
<!-- * -->
<!-- * So even if you don't personally like the way it looks, please -->
<!-- * don't change it to be non-bold - because it is a convention -->
<!-- * that is followed is the vast majority of existing man pages -->
<!-- * that document functions, and there's no good reason for us to -->
<!-- * be following it. -->

<xsl:template match="funcsynopsisinfo">
  <xsl:text>.PP&#10;</xsl:text>
  <xsl:apply-templates mode="bold" select="."/>
  <xsl:text>&#10;</xsl:text>
</xsl:template>

<xsl:template match="funcprototype">
  <xsl:variable name="funcprototype.string.value">
    <xsl:value-of select="funcdef"/>
  </xsl:variable>
  <xsl:variable name="funcprototype">
    <xsl:apply-templates select="funcdef"/>
  </xsl:variable>
  <xsl:text>.HP </xsl:text>
  <!-- * Hang Paragraph by length of string value of <funcdef> + 1 -->
  <!-- * (because funcdef is always followed by one open paren char) -->
  <xsl:value-of select="string-length (normalize-space ($funcprototype.string.value)) + 1"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:value-of select="normalize-space ($funcprototype)"/>
  <xsl:variable name="funcdef.suffix">
    <Funcdef.Suffix>(</Funcdef.Suffix>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($funcdef.suffix)"/>
  <xsl:apply-templates select="*[local-name() != 'funcdef']"/>
  <xsl:text>&#10;</xsl:text>
</xsl:template>

<xsl:template match="funcdef">
  <xsl:variable name="funcdef">
    <Funcdef>
      <xsl:apply-templates select="." mode="prevent.line.breaking"/>
    </Funcdef>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($funcdef)"/>
</xsl:template>

<xsl:template match="funcdef/function">
  <xsl:apply-templates mode="bold" select="."/>
</xsl:template>

<xsl:template match="void">
  <xsl:variable name="void">
    <Void>void);</Void>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($void)"/>
</xsl:template>

<xsl:template match="varargs">
  <xsl:variable name="varargs">
    <Varargs>...);</Varargs>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($varargs)"/>
</xsl:template>

<xsl:template match="paramdef">
  <xsl:variable name="paramdef">
    <Paramdef>
      <xsl:apply-templates mode="bold" select="." />
    </Paramdef>
  </xsl:variable>
  <xsl:apply-templates mode="prevent.line.breaking" select="exsl:node-set($paramdef)"/>
  <xsl:variable name="paramdef.suffix">
    <Paramdef.Suffix>
      <xsl:choose>
        <xsl:when test="following-sibling::*">
          <xsl:text>, </xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:text>);</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
    </Paramdef.Suffix>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($paramdef.suffix)"/>
</xsl:template>

<xsl:template match="paramdef/parameter">
  <xsl:apply-templates mode="italic" select="."/>
</xsl:template>

<xsl:template match="funcparams">
  <xsl:variable name="funcparams.prefix">
    <Funcparams.Prefix>(</Funcparams.Prefix>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($funcparams.prefix)"/>
  <xsl:apply-templates mode="bold" select="."/>
  <xsl:variable name="funcparams.suffix">
    <Funcparams.Suffix>)</Funcparams.Suffix>
  </xsl:variable>
  <xsl:apply-templates mode="bold" select="exsl:node-set($funcparams.suffix)"/>
</xsl:template>

</xsl:stylesheet>
