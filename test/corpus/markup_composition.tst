================================================================================
Composition.1 - Inline math inside bold
================================================================================
*here $\alpha$ inline*
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (bold
        open: (open)
        contents: (contents
          (inline_math_block
            open: (open)
            contents: (contents
              (expr))
            close: (close)))
        close: (close)))))

================================================================================
Composition.2 - Dollars inside code stay literal
================================================================================
=$x= text =$y=
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (code
        open: (open)
        contents: (contents
          (expr))
        close: (close))
      (expr)
      (code
        open: (open)
        contents: (contents
          (expr))
        close: (close)))))

================================================================================
Composition.3 - LaTeX command inside verbatim stays literal
================================================================================
~\sqrt{x}~
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (verbatim
        open: (open)
        contents: (contents
          (expr))
        close: (close)))))

================================================================================
Composition.4 - LaTeX command inside bold is recognized
================================================================================
*here \alpha there*
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (bold
        open: (open)
        contents: (contents
          (inline_latex
            command: (command)))
        close: (close)))))

================================================================================
Composition.5 - Dollars in three code spans do not pair as math
================================================================================
=$TMUX_FZF_ORDER=, asks =fzf=, runs =scripts/${item}.sh= ok.
--------------------------------------------------------------------------------

(document
  body: (body
    (paragraph
      (code
        open: (open)
        contents: (contents
          (expr)
          (expr)
          (expr)
          (expr)
          (expr))
        close: (close))
      (expr)
      (expr)
      (code
        open: (open)
        contents: (contents
          (expr))
        close: (close))
      (expr)
      (expr)
      (code
        open: (open)
        contents: (contents
          (expr)
          (expr)
          (expr))
        close: (close))
      (expr))))
