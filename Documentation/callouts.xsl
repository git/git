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
</xsl:stylesheet>
