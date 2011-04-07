<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

<!-- ********************************************************************
     $Id: block.xsl,v 1.7 2005/08/09 09:11:02 xmldoc Exp $
     ********************************************************************

     This file is part of the XSL DocBook Stylesheet distribution.
     See ../README or http://docbook.sf.net/release/xsl/current/ for
     copyright and other information.

     ******************************************************************** -->

<!-- ==================================================================== -->

<xsl:template match="caution|important|note|tip|warning">
  <xsl:call-template name="nested-section-title"/>
  <xsl:apply-templates/>
</xsl:template> 

<xsl:template match="formalpara">
  <xsl:call-template name="nested-section-title"/>
  <xsl:text>.RS 3&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>.RE&#10;</xsl:text>
</xsl:template>

<xsl:template match="para">
  <xsl:text>.PP&#10;</xsl:text>
  <xsl:call-template name="mixed-block"/>
  <xsl:text>&#10;</xsl:text>
</xsl:template>

<xsl:template match="simpara">
  <xsl:variable name="content">
    <xsl:apply-templates/>
  </xsl:variable>
  <xsl:value-of select="normalize-space($content)"/>
  <xsl:text>.sp&#10;</xsl:text>
</xsl:template>

<xsl:template match="address|literallayout|programlisting|screen|synopsis">
  <!-- * Yes, address and synopsis are verbatim environments. -->

  <xsl:choose>
    <!-- * Check to see if this verbatim item is within a parent element that -->
    <!-- * allows mixed content. -->
    <!-- * -->
    <!-- * If it is within a mixed-content parent, then a line space is -->
    <!-- * already added before it by the mixed-block template, so we don't -->
    <!-- * need to add one here. -->
    <!-- * -->
    <!-- * If it is not within a mixed-content parent, then we need to add a -->
    <!-- * line space before it. -->
    <xsl:when test="parent::caption|parent::entry|parent::para|
                    parent::td|parent::th" /> <!-- do nothing -->
    <xsl:otherwise>
      <xsl:text>.sp&#10;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:text>.nf&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>.fi&#10;</xsl:text>
  <!-- * if first following sibling node of this verbatim -->
  <!-- * environment is a text node, output a line of space before it -->
  <xsl:if test="following-sibling::node()[1][name(.) = '']">
    <xsl:text>.sp&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="informalexample">
  <xsl:text>.IP&#10;</xsl:text>
  <xsl:apply-templates/>
</xsl:template>

<!-- * suppress abstract -->
<xsl:template match="abstract"/>

</xsl:stylesheet>
