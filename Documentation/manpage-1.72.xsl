<!-- manpage-1.72.xsl:
     special settings for manpages rendered from asciidoc+docbook
     handles peculiarities in docbook-xsl 1.72.0 -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version="1.0">

<xsl:import href="manpage-base.xsl"/>

<!-- these are the special values for the roff control characters
     needed for docbook-xsl 1.72.0 -->
<xsl:param name="git.docbook.backslash">&#x2593;</xsl:param>
<xsl:param name="git.docbook.dot"      >&#x2302;</xsl:param>

<!-- these params silence some output from xmlto -->
<xsl:param name="man.output.quietly" select="1"/>
<xsl:param name="refentry.meta.get.quietly" select="1"/>

</xsl:stylesheet>
