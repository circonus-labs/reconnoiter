<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

<!-- ********************************************************************
     $Id: lists.xsl,v 1.21 2005/08/11 04:42:26 xmldoc Exp $
     ********************************************************************

     This file is part of the XSL DocBook Stylesheet distribution.
     See ../README or http://docbook.sf.net/release/xsl/current/ for
     copyright and other information.

     ******************************************************************** -->

<xsl:template match="para[ancestor::listitem or ancestor::step or ancestor::glossdef]|
	             simpara[ancestor::listitem or ancestor::step or ancestor::glossdef]|
		     remark[ancestor::listitem or ancestor::step or ancestor::glossdef]">
  <xsl:call-template name="mixed-block"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:if test="following-sibling::*[1][
                self::para or
                self::simpara or
                self::remark
                ]">
    <!-- * Make sure multiple paragraphs within a list item don't -->
    <!-- * merge together.                                        -->
    <xsl:text>.sp&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="variablelist|glosslist">
  <xsl:if test="title">
    <xsl:text>.PP&#10;</xsl:text>
    <xsl:apply-templates mode="bold" select="title"/>
    <xsl:text>&#10;</xsl:text>
  </xsl:if>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="varlistentry|glossentry">
  <xsl:text>.TP&#10;</xsl:text> 
  <!-- * read in contents of all terms or glossterms so that we can run -->
  <!-- * normalize-space on them as a set before rendering -->
  <xsl:variable name="content">
    <!-- * check each term/glossterm to see if it is the last one in the set; -->
    <!-- * if not last, render a comma and space after it so that multiple -->
    <!-- * terms/glossterms are displayed as a comma-separated list -->
    <xsl:for-each select="term|glossterm">
      <xsl:apply-templates/>
      <xsl:choose>
        <xsl:when test="position() = last()"/> <!-- do nothing -->
        <xsl:otherwise>
          <xsl:text>, </xsl:text>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:for-each>
  </xsl:variable>
  <xsl:value-of select="normalize-space($content)"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="varlistentry/term"/>
<xsl:template match="glossentry/glossterm"/>

<xsl:template match="variablelist[ancestor::listitem or ancestor::step or ancestor::glossdef]|
	             glosslist[ancestor::listitem or ancestor::step or ancestor::glossdef]">
  <xsl:text>.RS&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>.RE&#10;</xsl:text>
  <xsl:if test="following-sibling::node() or
                parent::para[following-sibling::node()] or
                parent::simpara[following-sibling::node()] or
                parent::remark[following-sibling::node()]">
    <xsl:text>.IP&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="varlistentry/listitem|glossentry/glossdef">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="itemizedlist/listitem">
  <!-- * We output a real bullet here (rather than, "\(bu", -->
  <!-- * the roff bullet) because, when we do character-map -->
  <!-- * processing before final output, the character-map will -->
  <!-- * handle conversion of the &#x2022; to "\(bu" for us -->
  <xsl:text>&#x2022;&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:if test="following-sibling::listitem">
    <xsl:text>.TP&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="orderedlist/listitem|procedure/step">
  <xsl:number format="1."/>
  <xsl:text>&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:if test="position()!=last()">
    <xsl:text>.TP&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="itemizedlist|orderedlist|procedure">
  <xsl:text>.TP 3&#10;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="itemizedlist[ancestor::listitem or ancestor::step  or ancestor::glossdef]|
	             orderedlist[ancestor::listitem or ancestor::step or ancestor::glossdef]|
		     procedure[ancestor::listitem or ancestor::step or ancestor::glossdef]">
  <xsl:text>.RS&#10;</xsl:text>
  <xsl:text>.TP 3&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>.RE&#10;</xsl:text>
  <xsl:if test="following-sibling::node() or
                parent::para[following-sibling::node()] or
                parent::simpara[following-sibling::node()] or
                parent::remark[following-sibling::node()]">
    <xsl:text>.IP&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<!-- ================================================================== -->
  
<!-- * for simplelist type="inline", render it as a comma-separated list -->
<xsl:template match="simplelist[@type='inline']">

  <!-- * if dbchoice PI exists, use that to determine the choice separator -->
  <!-- * (that is, equivalent of "and" or "or" in current locale), or literal -->
  <!-- * value of "choice" otherwise -->
  <xsl:variable name="localized-choice-separator">
    <xsl:choose>
      <xsl:when test="processing-instruction('dbchoice')">
	<xsl:call-template name="select.choice.separator"/>
      </xsl:when>
      <xsl:otherwise>
	<!-- * empty -->
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>

  <xsl:for-each select="member">
    <xsl:apply-templates/>
    <xsl:choose>
      <xsl:when test="position() = last()"/> <!-- do nothing -->
      <xsl:otherwise>
	<xsl:text>, </xsl:text>
	<xsl:if test="position() = last() - 1">
	  <xsl:if test="$localized-choice-separator != ''">
	    <xsl:value-of select="$localized-choice-separator"/>
	    <xsl:text> </xsl:text>
	  </xsl:if>
	</xsl:if>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:for-each>
  <xsl:text>&#10;</xsl:text>
</xsl:template>

<!-- * if simplelist type is not inline, render it as a one-column vertical -->
<!-- * list (ignoring the values of the type and columns attributes) -->
<xsl:template match="simplelist">
  <xsl:for-each select="member">
    <xsl:text>.IP&#10;</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>&#10;</xsl:text>
  </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
