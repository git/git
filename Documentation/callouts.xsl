<!-- callout.xsl: converts asciidoc callouts to man page format -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
<xsl:template match="co">
	<xsl:value-of select="concat('\fB(',substring-after(@id,'-'),')\fR')"/>
</xsl:template>
<xsl:template match="calloutlist">
	<xsl:text>.sp&#10;</xsl:text>
	<xsl:apply-templates/>
	<xsl:text>&#10;</xsl:text>
</xsl:template>
<xsl:template match="callout">
	<xsl:value-of select="concat('\fB',substring-after(@arearefs,'-'),'. \fR')"/>
	<xsl:apply-templates/>
	<xsl:text>.br&#10;</xsl:text>
</xsl:template>

<!-- sorry, this is not about callouts, but attempts to work around
 spurious .sp at the tail of the line docbook stylesheets seem to add -->
<xsl:template match="simpara">
  <xsl:variable name="content">
    <xsl:apply-templates/>
  </xsl:variable>
  <xsl:value-of select="normalize-space($content)"/>
  <xsl:if test="not(ancestor::authorblurb) and
                not(ancestor::personblurb)">
    <xsl:text>&#10;&#10;</xsl:text>
  </xsl:if>
</xsl:template>

<xsl:template match="literallayout[@class='monospaced']">
  <xsl:text>.RS</xsl:text>
  <xsl:if test="not($man.indent.width = '')">
    <xsl:text> </xsl:text>
    <xsl:value-of select="$man.indent.width"/>
  </xsl:if>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>&#10;.ft C&#10;.nf&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>&#10;.fi&#10;.ft&#10;</xsl:text>
  <xsl:text>.RE&#10;</xsl:text>
</xsl:template>

</xsl:stylesheet>
