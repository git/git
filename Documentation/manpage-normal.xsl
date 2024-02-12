<!-- manpage-normal.xsl:
     special settings for manpages rendered from asciidoc+docbook -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
		version="1.0">


<!-- these params silence some output from xmlto -->
<xsl:param name="man.output.quietly" select="1"/>
<xsl:param name="refentry.meta.get.quietly" select="1"/>

<!-- unset maximum length of title -->
<xsl:param name="man.th.title.max.length"/>

</xsl:stylesheet>
