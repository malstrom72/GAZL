/* Hand-generated value-returning parser for jspegTest.jspeg, per the _val-register emission rules.
   Validates the protocol (shared _val return register; $$ = per-rule local; tags = plain locals captured
   eagerly from _val; rule sets _val at its end iff it assigns $$) BEFORE the generator surgery. */

var parserSrc =
"(function(_s){\n" +
"function root($){return (function(){var _b=_i;return _($)&&expr($)&&(function(){var _l=_i,_x=(!!_s[_i])&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()}\n" +
"function expr($){var $$,$op,$r;return (function(){var _b=_i;return (product($)&&($$=_val,true))&&((function(){while((function(){var _b=_i;return (function(){var _m=_i;return (!!_s[_i]&&\"-+\".indexOf(_s[_i])>=0)&&(++_i,true)&&($op=_s.slice(_m,_i),true)})()&&_($)&&(product($)&&($r=_val,true))&&(function(){ if ($op == '+') $$ += $r; else $$ -= $r; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_val=$$,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()}\n" +
"function product($){var $$,$op,$r;return (function(){var _b=_i;return (group($)&&($$=_val,true))&&((function(){while((function(){var _b=_i;return (function(){var _m=_i;return (!!_s[_i]&&\"*/\".indexOf(_s[_i])>=0)&&(++_i,true)&&($op=_s.slice(_m,_i),true)})()&&_($)&&(group($)&&($r=_val,true))&&(function(){ if ($op == '*') $$ *= $r; else $$ /= $r; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_val=$$,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()}\n" +
"function group($){return (function(){var _b=_i;return number($)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===\"(\")&&(++_i,true)&&_($)&&expr($)&&(_s[_i]===\")\")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()}\n" +
"function number($){var $$;return (function(){var _b=_i;return (function(){var _m=_i;return ((function(){for(var _n=0;(!!_s[_i]&&\"0123456789\".indexOf(_s[_i])>=0)&&(++_i,true);++_n);return _n>0})())&&($$=_s.slice(_m,_i),true)})()&&(function(){ $$ = +$$ ; return true})()&&(_val=$$,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()}\n" +
"function _($){return (function(){var _b=_i;return ((function(){while((!!_s[_i]&&\" \\t\\r\\n\".indexOf(_s[_i])>=0)&&(++_i,true));})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()}\n" +
"var _i=0,_im=0,_val,_b=root({});\n" +
"return [_b,_val,(_b?_i:_im)];\n" +
"})\n";

var parse = eval(parserSrc);

var cases = [
  ["1+2*3", 7],
  ["2*3+1", 7],
  ["(1+2)*3", 9],
  ["10/2-3", 2],
  ["  1 + 2  ", 3],
  ["100", 100],
  ["2*3*4", 24],
  ["1+2+3+4", 10],
  ["(2+3)*(4-1)", 15]
];

var fails = 0;
for (var k = 0; k < cases.length; ++k) {
  var input = cases[k][0], expect = cases[k][1];
  var r = parse(input);
  var ok = r[0], val = r[1];
  var pass = (ok === true && val === expect);
  if (!pass) { ++fails; }
  console.log((pass ? "PASS" : "FAIL") + "  " + JSON.stringify(input) + " -> ok=" + ok + " val=" + val + " (expect " + expect + ")");
}
console.log(fails === 0 ? "\nALL PASS (" + cases.length + " cases)" : "\n" + fails + " FAILED");
