require 'asciidoctor'
require 'asciidoctor/extensions'

module Git
  module Documentation
    class LinkGitProcessor < Asciidoctor::Extensions::InlineMacroProcessor
      use_dsl

      named :chrome

      def process(parent, target, attrs)
        if parent.document.basebackend? 'html'
          prefix = parent.document.attr('git-relative-html-prefix')
          %(<a href="#{prefix}#{target}.html">#{target}(#{attrs[1]})</a>)
        elsif parent.document.basebackend? 'docbook'
          "<citerefentry>\n" \
            "<refentrytitle>#{target}</refentrytitle>" \
            "<manvolnum>#{attrs[1]}</manvolnum>\n" \
          "</citerefentry>"
        end
      end
    end
  end
end

Asciidoctor::Extensions.register do
  inline_macro Git::Documentation::LinkGitProcessor, :linkgit
end
