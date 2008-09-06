<xsl:stylesheet version="1.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output omit-xml-declaration="yes"/>

<xsl:template name="wrap-string">
    <xsl:param name="str" />
    <xsl:param name="wrap-col" />
    <xsl:param name="break-mark" />
    <xsl:param name="pos" select="0" />
    <xsl:choose>
        <xsl:when test="contains( $str, ' ' )">
            <xsl:variable name="before" select="substring-before( $str, ' ' )" />
            <xsl:variable name="pos-now" select="$pos + string-length( $before ) + 1" />
            <xsl:choose>
                <xsl:when test="$pos = 0" />
                <xsl:when test="floor( $pos div $wrap-col ) != floor( $pos-now div $wrap-col )">
                    <xsl:text xml:spacing="preserve">
</xsl:text><xsl:copy-of select="$break-mark" />
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text> </xsl:text>
                </xsl:otherwise>
            </xsl:choose>

            <xsl:value-of select="$before" />

            <xsl:call-template name="wrap-string">
                <xsl:with-param name="str" select="substring-after( $str, ' ' )" />
                <xsl:with-param name="wrap-col" select="$wrap-col" />
                <xsl:with-param name="break-mark" select="$break-mark" />
                <xsl:with-param name="pos" select="$pos-now" />
            </xsl:call-template>
        </xsl:when>
        <xsl:otherwise>
          <xsl:variable name="pos-now" select="$pos + string-length( $str )" />
          <xsl:choose>
            <xsl:when test="floor( $pos div $wrap-col ) != floor( $pos-now div $wrap-col )">
              <xsl:text xml:spacing="preserve">
</xsl:text><xsl:copy-of select="$break-mark" />
            </xsl:when>
            <xsl:otherwise>
              <xsl:text> </xsl:text>
            </xsl:otherwise>
          </xsl:choose>
          <xsl:value-of select="$str" />
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

<xsl:template match="parameter" name="configparams" xml:space="preserve">
    name: <xsl:value-of select="@name"/> (<xsl:value-of select="@required"/>) <xsl:if test="@default">[<xsl:value-of select="@default"/>]</xsl:if>
          allowed: /^<xsl:value-of select="@allowed"/>$/
          <xsl:call-template name="wrap-string" xml:space="default">
            <xsl:with-param name="str" select="."/>
            <xsl:with-param name="break-mark" select="'          '"/>
            <xsl:with-param name="wrap-col" select="'66'"/>
          </xsl:call-template>
</xsl:template>

<xsl:param name="example" select="0"/>
<xsl:template match="/">= <xsl:value-of select="module/name"/> =

<xsl:call-template name="wrap-string">
  <xsl:with-param name="str" select="module/description/*"/>
  <xsl:with-param name="break-mark" select="'    '"/>
  <xsl:with-param name="wrap-col" select="'70'"/>
</xsl:call-template>

  loader: <xsl:value-of select="module/loader"/>
  <xsl:if test="module/image">, image: <xsl:value-of select="module/image"/> </xsl:if>
  <xsl:if test="module/object">, object: <xsl:value-of select="module/object"/> </xsl:if>

  === Module Configuration ===
    <xsl:choose>
      <xsl:when  test="module/moduleconfig/parameter">
        <xsl:for-each select="module/moduleconfig/parameter">
          <xsl:call-template name="configparams" />
        </xsl:for-each>
      </xsl:when>
      <xsl:otherwise>No module-level options available for this module.</xsl:otherwise>
    </xsl:choose>

  === Check Configuration ===
    <xsl:choose>
      <xsl:when  test="module/checkconfig/parameter">
        <xsl:for-each select="module/checkconfig/parameter">
    <xsl:call-template name="configparams" />
        </xsl:for-each>
      </xsl:when>
      <xsl:otherwise>No check-level options available for this module.</xsl:otherwise>
    </xsl:choose>

  <xsl:if test="$example != 0">
  == Examples ==
  <xsl:for-each select="module/examples/example"><xsl:value-of disable-output-escaping="yes" select="."/></xsl:for-each>
  </xsl:if>
</xsl:template>
</xsl:stylesheet>
