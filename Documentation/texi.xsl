<!-- texi.xsl:
     convert refsection elements into refsect elements that docbook2texi can
     understand -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version="1.0">

<xsl:output method="xml"
	    encoding="UTF-8"
	    doctype-public="-//OASIS//DTD DocBook XML V4.5//EN"
	    doctype-system="http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd" />

<xsl:template match="//refsection">
	<xsl:variable name="element">refsect<xsl:value-of select="count(ancestor-or-self::refsection)" /></xsl:variable>
	<xsl:element name="{$element}">
		<xsl:apply-templates select="@*|node()" />
	</xsl:element>
</xsl:template>

<!-- Copy all other nodes through. -->
<xsl:template match="node()|@*">
	<xsl:copy>
		<xsl:apply-templates select="@*|node()" />
	</xsl:copy>
</xsl:template>

</xsl:stylesheet>
