/* Validate DUAL-MODE interop: a parser where some rules are HOLDER-style (value in $._, tags via sub-holders,
   ._ heuristic) and some are VALUE-style-bridged (local _v, bridge $._ = _v at the end). Both use $._ as the
   transfer slot, so they interoperate transparently. If arithmetic still computes, dual-mode is sound and the
   migration can be rule-by-rule.
   Mix here: number + expr are VALUE-style; product + group + root + _ are HOLDER-style. Both call directions
   cross the style boundary (holder product -> value number via group; value expr -> holder product). */
var bt = "||(_im=(_i>_im?_i:_im),_i=_b,false)";
var src =
"(function(_s){\n" +

// HOLDER-style root: passthrough, value = $._ (expr's)
"function root($){return (function(){var _b=_i;return _($)&&expr($)&&(function(){var _l=_i,_x=(!!_s[_i])&&(++_i,true);_i=_l;return !_x})()" + bt + "})()}\n" +

// VALUE-style expr (bridged): local _v accumulator; captures product's value from $._
"function expr($){var _v,$op,$r;return (function(){var _b=_i;return product($)&&(_v=$._,true)&&((function(){while((function(){var _b=_i;return (function(){var _m=_i;return (!!_s[_i]&&\"-+\".indexOf(_s[_i])>=0)&&(++_i,true)&&($op=_s.slice(_m,_i),true)})()&&_($)&&product($)&&($r=$._,true)&&(function(){ if($op==='+')_v+=$r;else _v-=$r;return true})()" + bt + "})());})(),true)&&($._=_v,true)" + bt + "})()}\n" +

// HOLDER-style product: value in $._, r:group via sub-holder $r, ._ heuristic ($r -> $r._)
"function product($){var $r={},$op;return (function(){var _b=_i;return group($)&&((function(){while((function(){var _b=_i;return (function(){var _m=_i;return (!!_s[_i]&&\"*/\".indexOf(_s[_i])>=0)&&(++_i,true)&&($op=_s.slice(_m,_i),true)})()&&_($)&&group($r)&&(function(){ if($op==='*')$._*=$r._;else $._/=$r._;return true})()" + bt + "})());})(),true)" + bt + "})()}\n" +

// HOLDER-style group: passthrough; calls value-style number and value-style expr
"function group($){return (function(){var _b=_i;return number($)&&_($)" + bt + "||(_s[_i]===\"(\")&&(++_i,true)&&_($)&&expr($)&&(_s[_i]===\")\")&&(++_i,true)&&_($)" + bt + "})()}\n" +

// VALUE-style number (bridged)
"function number($){var _v;return (function(){var _b=_i;return (function(){var _m=_i;return ((function(){for(var _n=0;(!!_s[_i]&&\"0123456789\".indexOf(_s[_i])>=0)&&(++_i,true);++_n);return _n>0})())&&(_v=_s.slice(_m,_i),true)})()&&(function(){ _v=+_v;return true})()&&($._=_v,true)" + bt + "})()}\n" +

// HOLDER-style _ : value-less
"function _($){return (function(){var _b=_i;return ((function(){while((!!_s[_i]&&\" \\t\\r\\n\".indexOf(_s[_i])>=0)&&(++_i,true));})(),true)" + bt + "})()}\n" +

"var _i=0,_im=0,_o={_:void 0},_b=root(_o);\n" +
"return [_b,_o._,(_b?_i:_im)];\n})\n";

var parse = eval(src);
var cases=[["1+2*3",7],["2*3+1",7],["(1+2)*3",9],["10/2-3",2],["  1 + 2  ",3],["100",100],["2*3*4",24],["1+2+3+4",10],["(2+3)*(4-1)",15]];
var fails=0;
for(var k=0;k<cases.length;++k){var r=parse(cases[k][0]);var pass=(r[0]===true&&r[1]===cases[k][1]);if(!pass)++fails;
  console.log((pass?"PASS":"FAIL")+"  "+JSON.stringify(cases[k][0])+" -> ok="+r[0]+" val="+r[1]+" (expect "+cases[k][1]+")");}
console.log(fails===0?"\nDUAL-MODE INTEROP ALL PASS ("+cases.length+")":"\n"+fails+" FAILED");
