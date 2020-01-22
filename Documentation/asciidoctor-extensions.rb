require 'asciidoctor'
require 'asciidoctor/extensions'

module Git
  module Documentation
    class LinkGitProcessor < Asciidoctor::Extensions::InlineMacroProcessor
      use_dsl

      named :chrome

      def process(parent, target, attrs)
        prefix = parent.document.attr('git-relative-html-prefix')
        if parent.document.doctype == 'book'
          "<ulink url=\"#{prefix}#{target}.html\">" \
          "#{target}(#{attrs[1]})</ulink>"
        elsif parent.document.basebackend? 'html'
          %(<a href="#{prefix}#{target}.html">#{target}(#{attrs[1]})</a>)
        elsif parent.document.basebackend? 'docbook'
          "<citerefentry>\n" \
            "<refentrytitle>#{target}</refentrytitle>" \
            "<manvolnum>#{attrs[1]}</manvolnum>\n" \
          "</citerefentry>"
        end
      end
    end

    class DocumentPostProcessor < Asciidoctor::Extensions::Postprocessor
      def process document, output
        if document.basebackend? 'docbook'
          mansource = document.attributes['mansource']
          manversion = document.attributes['manversion']
          manmanual = document.attributes['manmanual']
          new_tags = "" \
            "<refmiscinfo class=\"source\">#{mansource}</refmiscinfo>\n" \
            "<refmiscinfo class=\"version\">#{manversion}</refmiscinfo>\n" \
            "<refmiscinfo class=\"manual\">#{manmanual}</refmiscinfo>\n"
          output = output.sub(/<\/refmeta>/, new_tags + "</refmeta>")
        end
        output
      end
    end
  end
end

Asciidoctor::Extensions.register do
  inline_macro Git::Documentation::LinkGitProcessor, :linkgit
  postprocessor Git::Documentation::DocumentPostProcessor
end
