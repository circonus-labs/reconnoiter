<xsl:stylesheet version="1.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:template match="parameter" name="configparams">
  <variablelist>
    <varlistentry><term><xsl:value-of select="@name"/></term>
      <listitem>
        <variablelist>
          <varlistentry><term>required</term><listitem><para><xsl:value-of select="@required"/></para></listitem></varlistentry>
          <xsl:if test="@default">
          <varlistentry><term>default</term><listitem><para>
            <xsl:value-of select="@default"/>
            </para></listitem></varlistentry>
          </xsl:if>
          <varlistentry><term>allowed</term><listitem><para><xsl:value-of select="@allowed"/></para></listitem></varlistentry>
        </variablelist>
      <para>
        <xsl:value-of select="." />
      </para>
      </listitem>
    </varlistentry>
  </variablelist>
</xsl:template>

<xsl:template match="/">
  <section>
    <title><xsl:value-of select="module/name"/></title>
    <xsl:copy-of disable-output-escaping="yes" select="module/description/*"/>

    <variablelist>
      <varlistentry><term>loader</term><listitem><para><xsl:value-of select="module/loader"/></para></listitem></varlistentry>
      <xsl:if test="module/image">
      <varlistentry><term>image</term><listitem><para><xsl:value-of select="module/image"/></para></listitem></varlistentry>
      </xsl:if>
      <xsl:if test="module/object">
      <varlistentry><term>object</term><listitem><para><xsl:value-of select="module/object"/></para></listitem></varlistentry>
      </xsl:if>
    </variablelist>

  <section>
    <title>Module Configuration</title>
    <xsl:for-each select="module/moduleconfig/parameter">
      <xsl:call-template name="configparams" />
    </xsl:for-each>
  </section>

  <section>
    <title>Check Configuration</title>
    <xsl:for-each select="module/checkconfig/parameter">
      <xsl:call-template name="configparams" />
    </xsl:for-each>
  </section>

  <xsl:for-each select="module/examples/example">
    <xsl:copy-of select="."/>
  </xsl:for-each>
  </section>
</xsl:template>
</xsl:stylesheet>
