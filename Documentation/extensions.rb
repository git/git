require 'asciidoctor'
require 'asciidoctor/extensions'

module Git
  module Documentation
    class LinkGitProcessor < Asciidoctor::Extensions::InlineMacroProcessor
      use_dsl

      named :chrome

      def process(parent, target, attrs)
        if parent.document.basebackend? 'html'
          generate_html(parent, target, attrs)
        elsif parent.document.basebackend? 'docbook'
          generate_docbook(parent, target, attrs)
        end
      end

      private

      def generate_html(parent, target, attrs)
        section = attrs.has_key?(1) ? "(#{attrs[1]})" : ''
        prefix = parent.document.attr('git-relative-html-prefix') || ''
        %(<a href="#{prefix}#{target}.html">#{target}#{section}</a>\n)
      end

      def generate_docbook(parent, target, attrs)
        %(<citerefentry>
<refentrytitle>#{target}</refentrytitle><manvolnum>#{attrs[1]}</manvolnum>
</citerefentry>
)
      end
    end
  end
end

Asciidoctor::Extensions.register do
  inline_macro Git::Documentation::LinkGitProcessor, :linkgit
end
