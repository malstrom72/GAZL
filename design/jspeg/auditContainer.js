/* Go/no-go audit for the JSPEG 2 migration: find rules in impala.jspeg that MUTATE $$ (as a container:
   $$.field=, $$[..]=, $$ += ) but never INITIALIZE it ($$ = , $$: tag, $$= capture) in the same rule.
   Such a rule relies on an INHERITED $$ from an untagged parent call - the idiom the value-returning
   transform breaks (proven by tagCaptureTest). Zero flags => migration is uniform/mechanical. */
var fs = require('fs');
var src = fs.readFileSync('C:/Users/ClaudeRunner/git/GAZL-Impala2/impala/impala.jspeg', 'utf8');
var lines = src.split(/\r?\n/);

// Split into rules: a rule starts at a line whose first non-space begins at column 0 with `Name <-` or `Name  <-`.
var ruleStart = /^([A-Za-z_][A-Za-z0-9_]*)\s*<-/;
var rules = [];
var cur = null;
for (var i = 0; i < lines.length; ++i) {
  var m = lines[i].match(ruleStart);
  if (m) { cur = { name: m[1], line: i + 1, text: '' }; rules.push(cur); }
  if (cur) cur.text += lines[i] + '\n';
}

function classify(text) {
  var init = false, mutate = [];
  for (var k = 0; k < text.length - 1; ++k) {
    if (text[k] === '$' && text[k + 1] === '$') {
      var after = text.slice(k + 2);
      // skip the special $$parser. / $$s / $$i forms
      if (/^parser\./.test(after) || /^s\b/.test(after) || /^i\b/.test(after)) { k += 1; continue; }
      var t = after.replace(/^[ \t]*/, '');
      if (/^:/.test(t)) { init = true; }                               // $$:Rule tag
      else if (/^=[^=]/.test(t)) { init = true; }                      // $$ = ...  or  $$=Pattern
      else if (/^(\+=|-=|\*=|\/=)/.test(t)) { mutate.push('$$' + t.slice(0, 2)); }
      else if (/^\./.test(t)) {                                         // $$.field ...
        var mm = t.match(/^\.[A-Za-z_][A-Za-z0-9_]*\s*(=[^=]|\+=|-=|\*=|\/=)/);
        if (mm) mutate.push('$$' + t.slice(0, mm[0].length));
      } else if (/^\[/.test(t)) {                                       // $$[..] =
        var mb = t.match(/^\[[^\]]*\]\s*(=[^=]|\+=)/);
        if (mb) mutate.push('$$' + t.slice(0, Math.min(mb[0].length, 18)));
      }
      k += 1;
    }
  }
  return { init: init, mutate: mutate };
}

var flagged = [], mutatingRules = 0;
for (var r = 0; r < rules.length; ++r) {
  var c = classify(rules[r].text);
  if (c.mutate.length > 0) {
    mutatingRules++;
    if (!c.init) flagged.push({ name: rules[r].name, line: rules[r].line, samples: c.mutate.slice(0, 4) });
  }
}

console.log('rules total: ' + rules.length);
console.log('rules that mutate $$ as a container: ' + mutatingRules);
console.log('FLAGGED (mutate without init -> inherited-container idiom): ' + flagged.length);
for (var f = 0; f < flagged.length; ++f) {
  console.log('  ' + flagged[f].name + ' (line ' + flagged[f].line + ')  e.g. ' + flagged[f].samples.join(' , '));
}
console.log(flagged.length === 0 ? '\nGO: no inherited-container idiom; migration is uniform/mechanical.'
                                 : '\nNEEDS REWRITE: the above rules use the idiom the transform breaks.');
