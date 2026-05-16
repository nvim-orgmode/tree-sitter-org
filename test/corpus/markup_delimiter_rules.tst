================================================================================
DelimiterRules.1 - Pre-char alphanumeric rejects markup (mid-word star)
================================================================================
foo*bar* baz
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (expr)
      (expr)
      (expr)
      (expr)
      (expr))))

================================================================================
DelimiterRules.2 - Pre-char space accepts markup
================================================================================
text *bold* more
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (expr)
      (bold
        open: (open)
        contents: (contents)
        close: (close))
      (expr))))

================================================================================
DelimiterRules.3 - Post-char [ accepted (deliberate Emacs divergence)
================================================================================
*bold*[next
--------------------------------------------------------------------------------

;; Emacs rejects this ([ not in post-char whitelist).
;; We accept it: lets *bold*[[url][desc]] parse as bold + separate link,
;; which is ergonomic for emphasis directly before a link. See
;; research/tree-sitter-org-markup-grammar.org section "Deliberate
;; Emacs divergences" for rationale.
(document
  body: (body
    (paragraph
      (bold
        open: (open)
        contents: (contents)
        close: (close))
      (expr))))

================================================================================
DelimiterRules.3b - Bold directly before link (real-world use case)
================================================================================
*important*[[https://x.org][desc]]
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (bold
        open: (open)
        contents: (contents)
        close: (close))
      (link_desc
        url: (expr)
        desc: (expr)))))

================================================================================
DelimiterRules.4 - Post-char dot accepts markup
================================================================================
*bold*.
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (bold
        open: (open)
        contents: (contents)
        close: (close))
      (expr))))

================================================================================
DelimiterRules.5 - Body-border comma accepted (deliberate Emacs divergence)
================================================================================
text *foo,* end
--------------------------------------------------------------------------------

;; Emacs rejects this (body-border rule forbids "," directly before close).
;; We accept it: the rule is an Emacs historical quirk with little practical
;; value. See research/tree-sitter-org-markup-grammar.org section "Deliberate
;; Emacs divergences" for rationale.
(document
  body: (body
    (paragraph
      (expr)
      (bold
        open: (open)
        contents: (contents)
        close: (close))
      (expr))))

================================================================================
DelimiterRules.6 - Headline disambig: col=0 single star is headline
================================================================================
* Heading
--------------------------------------------------------------------------------

(document
  subsection: (section
    headline: (headline
      stars: (stars)
      item: (item
        (expr)))))
