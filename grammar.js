const asciiSymbols = [ '!', '"', '#', '$', '%', '&', "'", '(', ')', '*',
  '+', ',', '-', '.', '/',  ':', ';', '<', '=', '>', '?', '@', '[', ']',
  '\\', '^', '_', '`', '{', '|', '}', '~' ]
const markupDelimiters = '*/_+=~'

const org_grammar = {
  name: 'org',
  // Treat newlines explicitly, all other whitespace is extra
  extras: _ => [/[ \f\t\v\u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000\ufeff]/],

  externals: $ => [
    $._liststart,
    $._listend,
    $._listitemend,
    $.bullet,
    $._stars,
    $._sectionend,
    $._eof,  // Basically just '\0', but allows multiple to be matched
    $._link_open,
    $._footnote_open,
    $._fndef_open,
    $.latex_math_single_dollar,
    $.text_dollar,
    $._bold_open,
    $._bold_close,
    $._italic_open,
    $._italic_close,
    $._underline_open,
    $._underline_close,
    $._strikethrough_open,
    $._strikethrough_close,
    $._code_open,
    $._code_close,
    $._verbatim_open,
    $._verbatim_close,
    $.error_sentinel
  ],

  inline: $ => [
    $._nl,
    $._eol,
    $._ts_contents,
    $._directive_list,
    $._body_contents,
  ],

  precedences: _ => [
    ['document_directive', 'body_directive'],
    ['special', 'immediate', 'non-immediate'],
  ],

  conflicts: $ => [

    // stars  'headline_token1'  item_repeat1  •  ':'  …
    // Should we start the tag?
    [$.item],

    [$._tag_expr_start, $.expr],
    // _multiline_text  •  ':'  …
    // Is the ':' continued multiline text or is it a drawer?
    [$.paragraph],
    [$.fndef],
    // ':'  'str'  …
    // Continue the conflict from above
    [$.expr, $.drawer],

    // headline  'entry_token1'  ':'  •  '<'  …
    [$.entry, $.expr],
    [$.entry, $._markup],
  ],

  rules: {

    document: $ => seq(
      optional(field('body', $.body)),
      repeat(field('subsection', $.section)),
    ),

    // Set up to prevent lexing conflicts of having two paragraphs in a row
    body: $ => $._body_contents,

    _body_contents: $ => choice(
      repeat1($._nl),
      seq(repeat($._nl), $._multis),
      seq(
        repeat($._nl),
        repeat1(seq(
          choice(
            seq($._multis, $._nl),
            seq(optional(choice($.fndef, $.paragraph)), $._element),
          ),
          repeat($._nl),
        )),
        optional($._multis)
      ),
    ),

    link: $ => seq(
      alias($._link_open, '[['),
      field('url', repeat(alias($._expr_with_space, $.expr))),
      token(']]')
    ),

    link_desc: $ => seq(
      alias($._link_open, '[['),
      field('url', repeat(alias($._expr_with_space, $.expr))),
      token(']['),
      field('desc', repeat(alias($._expr_with_space, $.expr))),
      token(']]')
    ),

    priority: _ => token(/\[#\w+\]/),

    inline_code_block: $ => seq(
      field('open', alias($._inline_code_open, $.open)),
      field('contents', alias(repeat($.expr), $.contents)),
      field('close', alias(choice(token('}'), token.immediate('}')), $.close))
    ),

    _inline_code_open: $ => choice(
      token(/src_([^\s\[\{]+)\{/),
      token(/src_[^\s\[\{]+\[[^\r\n\[\]]*\]\{/)
    ),

    inline_math_block: $ => choice(
      seq(
        field('open', alias('\\(', $.open)),
        field('contents', alias(repeat($.expr), $.contents)),
        field('close', alias('\\)', $.close))
      ),
      seq(
        field('open', alias($.latex_math_single_dollar, $.open)),
        field('contents', alias(repeat($.expr), $.contents)),
        field('close', alias($.latex_math_single_dollar, $.close))
      ),
    ),

    inline_latex: $ => prec.right(seq(
      field('command', alias(token(/\\\p{L}+\*?/u), $.command)),
      repeat(choice(
        field('option', $.latex_optional_argument),
        field('argument', $.latex_argument),
      )),
    )),

    latex_argument: $ => seq(
      field('open', alias(token.immediate('{'), $.open)),
      field('contents', alias(repeat(choice(
        $.expr,
        $.inline_latex,
        $.latex_argument,
        $.latex_optional_argument,
      )), $.contents)),
      field('close', alias(choice(token('}'), token.immediate('}')), $.close)),
    ),

    latex_optional_argument: $ => seq(
      field('open', alias(token.immediate('['), $.open)),
      field('contents', alias(repeat(choice(
        $.expr,
        $.inline_latex,
        $.latex_argument,
        $.latex_optional_argument,
      )), $.contents)),
      field('close', alias(choice(token(']'), token.immediate(']')), $.close)),
    ),

    display_math_block: $ => choice(
      seq(
        field('open', alias('\\[', $.open)),
        field('contents', alias(repeat($.expr), $.contents)),
        field('close', alias('\\]', $.close))
      ),
      seq(
        field('open', alias('$$', $.open)),
        field('contents', alias(repeat($.expr), $.contents)),
        field('close', alias('$$', $.close))
      ),
    ),

    // Can't have multiple in a row (fnDefs are an exception and can be consecutive)
    _multis: $ => choice(
      seq($.fndef, repeat($.fndef)),
      $.paragraph,
      $._directive_list,
    ),

    _element: $ => choice(
      $.comment,
      // Have attached directive:
      $.drawer,
      $.list,
      $.block,
      $.dynamic_block,
      $.table,
      $.latex_env,
    ),

    section: $ => seq(
      field('headline', $.headline),
      optional(field('plan', $.plan)),
      optional(field('property_drawer', $.property_drawer)),
      optional(field('body', $.body)),
      repeat(field('subsection', $.section)),
      $._sectionend,
    ),

    stars: $ => seq($._stars, /\*+/),

    headline: $ => seq(
      field('stars', $.stars),
      /[ \t]+/, // so it's not part of (item)
      optional(field('item', $.item)),
      optional(field('tags', $.tag_list)),
      $._eol,
    ),

    item: $ => choice(
      seq($.expr, field('priority', $.priority), repeat1($._markup)),
      seq(field('priority', $.priority), repeat($._markup)),
      repeat1($._markup)
    ),

    tag_list: $ => prec.dynamic(1, seq(
      $._tag_expr_start,
      repeat1(seq(
        field('tag', alias($._noc_expr, $.tag)),
        token.immediate(prec('special', ':')),
      )),
    )),

    // This is in another node to ensure a conflict with headline (item)
    _tag_expr_start: _ => token(prec('non-immediate', ':')),

    property_drawer: $ => seq(
      alias(/:properties:/i, ':properties:'),
      repeat1($._nl),
      repeat(seq($.property, repeat1($._nl))),
      prec.dynamic(1, alias(/:end:/i, ':end:')),
      $._eol,
    ),

    property: $ => seq(
      ':',
      field('name', alias($._immediate_expr, $.expr)),
      token.immediate(':'),
      field('value', optional(alias($._expr_line, $.value)))
    ),

    plan: $ => seq(repeat1($.entry), prec.dynamic(1, $._eol)),

    entry: $ => seq(
      optional(seq(
        field('name', alias(token(prec('non-immediate', /\p{L}+/)), $.entry_name)),
        token.immediate(prec('immediate', ':'))
      )),
      field('timestamp', $.timestamp)
    ),

    timestamp: $ => choice(
      seq(token(prec('non-immediate', '<')), $._ts_contents, '>'),
      seq(token(prec('non-immediate', '<')), $._ts_contents, '>--<', $._ts_contents, '>'),
      seq(token(prec('non-immediate', '[')), $._ts_contents, ']'),
      seq(token(prec('non-immediate', '[')), $._ts_contents, ']'),
      seq(token(prec('non-immediate', '[')), $._ts_contents, ']--[', $._ts_contents, ']'),
      seq('<%%', $.tsexp, token(prec('special', '>'))),
      seq('[%%', $.tsexp, token(prec('special', ']'))),
    ),
    tsexp: $ => repeat1(alias($._ts_expr, $.expr)),

    _ts_contents: $ => seq(
      field('date', $.date),
      repeat($._ts_element),
    ),

    date: $ => /\p{N}{1,4}-\p{N}{1,4}-\p{N}{1,4}/,

    _ts_element: $ => choice(
      field('day', alias(/\p{L}[^\]>\p{Z}\t\n\r]*/, $.day)),
      field('time', alias(/\p{N}?\p{N}[:.]\p{N}\p{N}( ?\p{L}{1,2})?/, $.time)),
      field('duration', alias(/\p{N}?\p{N}[:.]\p{N}\p{N}( ?\p{L}{1,2})?-\p{N}?\p{N}[:.]\p{N}\p{N}( ?\p{L}{1,2})?/, $.duration)),
      field('repeat', alias(/[.+]?\+\p{N}+\p{L}/, $.repeat)),
      field('delay', alias(/--?\p{N}+\p{L}/, $.delay)),
      alias(prec(-1, /[^\[<\]>\p{Z}\t\n\r]+/), $.expr),
    ),

    paragraph: $ => seq(optional($._directive_list), $._multiline_text),

    fndef: $ => prec.dynamic(1, seq(
      optional($._directive_list),
      seq(
        alias($._fndef_open, '[fn:'),
        field('label', alias(token.immediate(/[^\p{Z}\t\n\r\]]+/), $.expr)),
        token.immediate(']'),
      ),
      field('description', alias($._multiline_text, $.description))
    )),

    footnote_reference: $ => prec(-1, seq(
      alias($._footnote_open, '[fn:'),
      field('label', alias(token.immediate(/[^\p{Z}\t\n\r\]]+/), $.expr)),
      token.immediate(']'),
    )),

    _directive_list: $ => repeat1(field('directive', $.directive)),
    directive: $ => seq(
      '#+',
      field('name', alias($._immediate_expr, $.expr)),
      token.immediate(':'),
      field('value', optional(alias($._expr_line, $.value))),
      $._eol,
    ),

    comment: $ => prec.right(repeat1(seq(/#[^+\n\r]/, repeat($.expr), $._eol))),

    drawer: $ => seq(
      optional($._directive_list),
      token(prec('non-immediate', ':')),
      field('name', alias($._noc_expr, $.expr)),
      token.immediate(prec('special', ':')),
      $._nl,
      optional(field('contents', $.contents)),
      prec.dynamic(1, alias(/:end:/i, ':end:')),
      $._eol,
    ),

    block: $ => seq(
      optional($._directive_list),
      alias(/#\+begin_/i, '#+begin_'),
      field('name', $.expr),
      optional(repeat1(field('parameter', $.expr))),
      $._nl,
      optional(field('contents', alias($._block_contents, $.contents))),
      alias(/#\+end_/i, '#+end_'),
      field('end_name',alias($._immediate_expr, $.expr)),
      $._eol,
    ),

    dynamic_block: $ => seq(
      optional($._directive_list),
      alias(/#\+begin:/i, '#+begin:'),
      field('name', $.expr),
      repeat(field('parameter', $.expr)),
      $._nl,
      optional(field('contents', alias($._block_contents, $.contents))),
      alias(/#\+end:/i, '#+end:'),
      optional(field('end_name', $.expr)),
      $._eol,
    ),

    _block_contents: $ => seq(
      optional(repeat1($.expr)),
      repeat1($._nl),
      repeat(seq(repeat1($.expr), repeat1($._nl))),
    ),

    list: $ => seq(
      optional($._directive_list),
      $._liststart,  // captures indent length and bullet type
      repeat(seq($.listitem, $._listitemend, repeat($._nl))),
      seq($.listitem, $._listend)
    ),

    listitem: $ => seq(
      field('bullet', $.bullet),
      optional(field('checkbox', $.checkbox)),
      choice(
        $._eof,
        field('contents', $._body_contents),
      ),
    ),

    checkbox: $ => choice(
      '[ ]',
      seq(
        token(prec('non-immediate', '[')),
        field('status', alias($._checkbox_status_expr, $.expr)),
        token.immediate(prec('special', ']')),
      ),
    ),

    table: $ => prec.right(seq(
      optional($._directive_list),
      repeat1(choice($.row, $.hr)),
      repeat($.formula),
    )),

    row: $ => prec(1, seq(
      repeat1($.cell),
      optional(token(prec(1, '|'))),
      $._eol,
    )),

    cell: $ => seq(
      token(prec(1, '|')), // Table > paragraph (expr)
      optional(field('contents', alias($._expr_line, $.contents))),
    ),
    hr: $ => seq(
      token(prec(1, '|')),
      repeat1(seq(token.immediate(prec(1, /[-+]+/)), optional('|'))),
      $._eol,
    ),

    formula: $ => seq(
      alias(/#\+tblfm:/i, '#+tblfm:'),
      field('formula', optional($._expr_line)),
      $._eol,
    ),

    latex_env: $ => seq(
      optional($._directive_list),
      choice(
        seq(
          alias(/\\begin\{/i, '\\begin{'),
          field('name', alias(/[\p{L}\p{N}*]+/, $.name)),
          token.immediate('}'),
          $._nl,
          optional(field('contents', alias($._block_contents, $.contents))),
          alias(/\\end\{/i, '\\end{'),
          alias(/[\p{L}\p{N}*]+/, $.name),
          token.immediate('}'),
        ),
        seq(
          token(seq('\\[', choice('\n', '\r'))),
          optional(field('contents', alias($._block_contents, $.contents))),
          '\\]',
        ),
        seq(
          token(seq('\\(', choice('\n', '\r'))),
          optional(field('contents', alias($._block_contents, $.contents))),
          '\\)',
        ),
      ),
      $._eol,
    ),

    contents: $ => seq(
      optional($._expr_line),
      repeat1($._nl),
      repeat(seq($._expr_line, repeat1($._nl))),
    ),

    _nl: _ => choice('\n', '\r'),
    _eol: $ => choice('\n', '\r', $._eof),

    _expr_line: $ => repeat1($._markup),
    _multiline_text: $ => repeat1(seq(
      repeat1($._markup),
      $._eol
    )),

    _markup: $ => choice(
      $.bold,
      $.italic,
      $.underline,
      $.strikethrough,
      $.code,
      $.verbatim,
      $.inline_code_block,
      $.inline_math_block,
      $.inline_latex,
      $.display_math_block,
      $.link,
      $.link_desc,
      $.timestamp,
      $.footnote_reference,
      $.citation,
      $.expr,
    ),

    bold: $ => prec.right(seq(
      field('open', alias($._bold_open, $.open)),
      field('contents', alias(repeat1(nestedMarkup($)), $.contents)),
      field('close', alias($._bold_close, $.close))
    )),

    italic: $ => prec.right(seq(
      field('open', alias($._italic_open, $.open)),
      field('contents', alias(repeat1(nestedMarkup($)), $.contents)),
      field('close', alias($._italic_close, $.close))
    )),

    underline: $ => prec.right(seq(
      field('open', alias($._underline_open, $.open)),
      field('contents', alias(repeat1(nestedMarkup($)), $.contents)),
      field('close', alias($._underline_close, $.close))
    )),

    strikethrough: $ => prec.right(seq(
      field('open', alias($._strikethrough_open, $.open)),
      field('contents', alias(repeat1(nestedMarkup($)), $.contents)),
      field('close', alias($._strikethrough_close, $.close))
    )),

    code: $ => seq(
      field('open', alias($._code_open, $.open)),
      field('contents', alias(repeat1($.expr), $.contents)),
      field('close', alias($._code_close, $.close))
    ),

    verbatim: $ => seq(
      field('open', alias($._verbatim_open, $.open)),
      field('contents', alias(repeat1($.expr), $.contents)),
      field('close', alias($._verbatim_close, $.close))
    ),

    citation: $ => choice(
      seq(
        alias(token(prec('special', '[cite:')), '[cite:'),
        repeat(alias(token(/[^@\]\n\r]+/), $.expr)), // prefix
        field('reference', $.citation_reference), // see at least one reference
        repeat(choice(
          field('reference', $.citation_reference),
          alias(token(/[^@\]\n\r]+/), $.expr),
        )),
        ']',
      ),
      seq(
        alias(token(prec('special', '[cite/')), '[cite/'),
        field('style', alias(token.immediate(/[^\s:]+/), $.expr)),
        token.immediate(':'),
        repeat(alias(token(/[^@\]\n\r]+/), $.expr)), // prefix
        field('reference', $.citation_reference), // see at least one reference
        repeat(choice(
          field('reference', $.citation_reference),
          alias(token(/[^@\]\n\r]+/), $.expr),
        )),
        ']',
      ),
    ),

    citation_reference: $ => seq(
      '@',
      field('key', alias(token.immediate(/[\p{L}\p{N}!$&*+./:<>?^_`|-]+/u), $.expr)),
    ),

    _immediate_expr: $ => repeat1(expr('immediate', token.immediate)),
    _noc_expr: $ => repeat1(expr('immediate', token.immediate, ':')),

    _checkbox_status_expr: $ => expr('immediate', token.immediate, ']'),

    _ts_expr: $ => seq(
      expr('non-immediate', token, '>]'),
      repeat(expr('immediate', token.immediate, '>]'))
    ),

    expr: $ => choice(
      token(prec(-1, /[*\/_+=~]/)),
      seq(
        expr('non-immediate', token, markupDelimiters),
        repeat(expr('immediate', token.immediate, markupDelimiters))
      )
    ),

    _expr_with_space: $ => seq(
      expr('non-immediate', token, '', ' '),
      repeat(expr('immediate', token.immediate, '', ' '))
    ),

  }
};

function expr(pr, tfunc, skip = '', extra = '') {
  skip = skip.split("")
  extra = extra.split("")
  const excluded = skip.length === 0 ? '' : escapeForRegexCharClass(skip.join(''))
  return choice(
    ...asciiSymbols.filter(c => !skip.includes(c)).map(c => tfunc(prec(pr, c))),
    ...extra.map(c => tfunc(prec(pr, c))),
    alias(tfunc(prec(pr, /\p{L}+/)), 'str'),
    alias(tfunc(prec(pr, /\p{N}+/)), 'num'),
    alias(tfunc(prec(pr, new RegExp(`[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r${excluded}]`, 'u'))), 'sym'),
     // for checkboxes: ugly, but makes them work..
    // alias(tfunc(prec(pr, 'x')), 'str'),
    // alias(tfunc(prec(pr, 'X')), 'str'),
  )
}

function escapeForRegexCharClass(text) {
  return text.replace(/[\\\-\]\^]/g, '\\$&')
}

function nestedExpr() {
  return alias(token(/[^\s*\/_+=~]+/), 'expr')
}

function nestedMarkup($) {
  return choice(
    $.bold,
    $.italic,
    $.underline,
    $.strikethrough,
    $.code,
    $.verbatim,
    nestedExpr(),
    $.inline_code_block,
    $.inline_math_block,
    $.inline_latex,
    $.display_math_block,
    $.link,
    $.link_desc,
    $.timestamp,
    $.footnote_reference,
    $.citation,
  )
}

module.exports = grammar(org_grammar);
