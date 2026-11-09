================================================================================
SpanExtent.1 - Blank line ends markup (paragraph break)
================================================================================
text *bold

other paragraph* end
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (expr)
      (expr)
      (expr))
    (paragraph
      (expr)
      (expr)
      (expr)
      (expr))))

================================================================================
SpanExtent.2 - Single-line markup still works (regression guard)
================================================================================
text *bold* end
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
SpanExtent.3 - Two newlines inside markup not allowed even without blank-line content
================================================================================
*bold


end*
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (expr)
      (expr))
    (paragraph
      (expr)
      (expr))))
