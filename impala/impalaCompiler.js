var $$parser = {};
var impalaCompilerImpl = (function(_s, _options) {
var _hostOptions = _options || {};
var KEYWORD_WORDS = [
	'abs', 'array', 'assert', 'case', 'const', 'copy', 'default', 'do', 'else', 'export', 'extern',
	'float', 'floor', 'for', 'from', 'ftoi', 'funcptr', 'functype', 'function', 'global', 'goto', 'if',
	'import', 'int', 'itof', 'locals', 'loop', 'native', 'null', 'nullfunc', 'pointer', 'readonly',
	'returns', 'sizeof', 'struct', 'switch', 'temporary', 'to', 'while'
];
var output = (typeof _hostOptions.output === 'function') ? _hostOptions.output : function () {};
var hostRandomId = Object.prototype.hasOwnProperty.call(_hostOptions, 'randomId')
	? _hostOptions.randomId
	: undefined;
$$parser.sourceName = Object.prototype.hasOwnProperty.call(_hostOptions, 'sourceName')
	? _hostOptions.sourceName
	: undefined;
{
    /**
     * map(target, k1, v1, k2, v2, ...)
     *   assigns target[k1]=v1, etc.
     */
    function map(target /*, k1, v1, ... */) {
        for (var i = 1; i + 1 < arguments.length; i += 2) {
            target[ arguments[i] ] = arguments[i+1];
        }
        return target;
    }

    /**
     * Deep-clone an object or array (only handles plain objects & arrays)
     */
    function clone(o) {
        // primitives, functions, null
        if (typeof o !== 'object' || o === null) {
            return o;
        }
        // array vs object
        var r = (o instanceof Array) ? [] : {};
        for (var k in o) {
            // only own props (in ES3 all enumerable are “own” unless on prototype)
            var v = o[k];
            r[k] = (typeof v === 'object' && v !== null ? clone(v) : v);
        }
        return r;
    }


    /** template baker: "Hello {name}" -> eval expressions in {...} */
    function bake(str) {
        // same pattern as /\{([^}]+)\}/g  but expressed as a string because poor jspeg parsing
        var re = new RegExp("\\{([^}]+)\\}", "g");
        return str.replace(re, function (_, expr) {
            return eval(expr);
        });
    }

    /** simple assertion */
    function assert(cond, msg) {
        if (!cond) throw new Error("Assertion failed" + (msg ? ": " + msg : ""));
    }

    /** turn arguments into a real Array (optional from-index) */
    function args(src, from) {
        var start = (arguments.length > 1 ? from : 0);
        var a = [];
        for (var i = start; i < src.length; i++) {
            a.push(src[i]);
        }
        return a;
    }

    /** string utilities */
    function replace(str, sub, by) {
        return str.split(sub).join(by);
    }
    function find(str, chars) {
        var i = 0;
        while (i < str.length && chars.indexOf(str[i]) < 0) {
            i++;
        }
        return i;
    }
    function span(str, chars) {
        var i = 0;
        while (i < str.length && chars.indexOf(str[i]) >= 0) i++;
        return i;
    }
    function rspan(str, chars) {
        var i = str.length;
        while (i > 0 && chars.indexOf(str[i-1]) >= 0) i--;
        return i;
    }
    function char(code) {
        return String.fromCharCode(code & 0xFF);
    }
    function ordinal(ch) {
        return ((ch + "").charCodeAt(0)) & 0xFF;
    }

    function evaluate(str) {
        return JSON.parse(str);
    }

    /** queue ops on plain Arrays */
    function resetQueue(q) {
        q.length = 0;
    }
    function queueSize(q) {
        return q.length;
    }
    function pushBack(q, v) {
        q.push(v);
    }
    function popBack(q) {
        if (q.length === 0) throw new Error("Queue underrun");
        return q.pop();
    }
    function pushFront(q, v) {
        q.unshift(v);
    }
    function popFront(q) {
        if (q.length === 0) throw new Error("Queue underrun");
        return q.shift();
    }

    /** math/random helpers */
    function random() {
        return Math.random();
    }
    function floor(x) {
        return Math.floor(x);
    }
    function time() {
        return (new Date()).getTime();
    }


    /* --------------------------------
     *  Impala-JSPEG  ▸  core tables / helpers  (ES3)
     *  arrays now rely on .length / .push
     * -------------------------------- */

    /* 1  constants & simple flags */
    var IMPALA_VERSION = '1.0';
    var dry            = false;
    var legacyMode     = (typeof _hostOptions !== 'undefined' && _hostOptions != null
            && !!_hostOptions.legacy);                          /// `--legacy` downgrades strict-expression errors to warnings

    /* 2  make sure the buckets exist */
    var META_TO_GAZL   = {};
    var SUPPORTED_OPS  = {};
    var CASTS_TO_TYPES = {};
    var ZEROES = {};
    var TYPE_SUFFIXES  = {};
    var VERBOSE_TYPES  = {};
    var sourceName = undefined;
    var metacode = [];
    var strings = { s:[], a:[] };
    var labelCounter = 0;
    var stock = { '%': [], '<': [] };
    var counters = { '%': 0,  '<': 0  };
    var symbols = { 'locals': {}, 'globals': {}, 'functions': {}, 'defines': {} };
    var structs = {};                                    /// name -> { fields:[{name,type,elem,offset,words}], words, complete }
    var functypes = {};                                  /// name -> signature { params, returnList, returnCount, returns, returnWords, complete }
    var switchStack = [];
    var noForward = false;
    var destructuring = false;                           /// true while parsing the call on the RHS of `a, b = f(...)`
    var destTargets = [];                                /// collected LHS targets during a destructuring assignment
    var exportNext = false;                              /// set while parsing an `export`-prefixed declaration (Step 5)

    /* 3  bulk-fill the lookup tables */
    map(META_TO_GAZL,
        '=',   'MOV?', ':=',  'MOV?',
        '=itof','iTOf', '=ftoi','fTOi', '=abs','ABS?', '=floor','FLOf',
        '=[]','PEEK',  '[]=','POKE',   '=[]$','GETL',  '[]$=','SETL',
        '=*','PEEK', ':=*','PEEK', '*=','POKE', '=&','ADRL', 'copy','COPY',
        '-->','GOTO', '-->#','SWCH',  '...','FOR?',  '()','CALL', '--^','RETU',
        '|','IOR?', '&','AND?', '^','XOR?', '<<','SHL?', '>>>','SHRu', '>>','SHR?',
        '+','ADD?', '-','SUB?', '*','MUL?', '/','DIV?', '%','MOD?', 'd','DIFp',
        '<=','LEQ?', '<','LSS?', '>=','GEQ?', '>','GRT?', '!=','NEQ?', '==','EQU?',
        '!<=','GRT?','!<','GEQ?','!>=','LSS?','!>','LEQ?','!!=','EQU?','!==','NEQ?'
    );

    map(SUPPORTED_OPS,
        '=*p','?', '=&f','p', '=&F','p', '=&i','p', '=&p','p', '=&?','p',
        '=-i','i', '=-f','f', '=~i','i',
        '=floatf','f', '=float?','f', '=funcptrF','F', '=funcptr?','F',
        '=inti','i', '=int?','i', '=pointerp','p', '=pointer?','p',
        '=absi','i', '=absf','f', '=itofi','f', '=ftoif','i', '=floorf','f',
        '|ii','i', '&ii','i', '^ii','i', '<<ii','i', '>>>ii','i', '>>ii','i',
        '+ii','i', '-ii','i', '*ii','i', '/ii','i', '%ii','i',
        '+ff','f', '-ff','f', '*ff','f', '/ff','f',
        '+pi','p', '-pi','p', '-pp','i',
        '=[]pi','?',
        '<=ii','i', '<ii','i', '>=ii','i', '>ii','i', '!=ii','i', '==ii','i',
        '<=ff','f', '<ff','f', '>=ff','f', '>ff','f', '!=ff','f', '==ff','f',
        '<=pp','p', '<pp','p', '>=pp','p', '>pp','p', '!=pp','p', '==pp','p',
        '<=FF','F', '<FF','F', '>=FF','F', '>FF','F', '!=FF','F', '==FF','F'
    );

    map(CASTS_TO_TYPES, 'float','f','funcptr','F','int','i','pointer','p');
    map(ZEROES,         'f','#0.0','i','#0','p','&NULL','F','&NULL');
    map(TYPE_SUFFIXES,  'void','', 'i','i','f','f','p','p','F','p','U','',
                                 'N','',   'A','A','?','E');
    map(VERBOSE_TYPES,  'i','int','f','float','p','pointer','F','funcptr',
                                 'U','function','N','native','A','array','?','untyped');

    function signatureParamCategory(type) {
        switch (type) {
            case 'i': return 'int';
            case 'f': return 'float';
            case 'p': return 'ptr';
            case 'F': return 'funcptr';
            default:  return 'unknown';
        }
    }

    /* render a full element descriptor as a single metadata token: 'p:i' -> "int-ptr",
       'p:p:i' -> "int-ptr-ptr", 'i' -> "int", 'p' -> "ptr" (element-unknown pointer) */
    function signatureCategoryForDesc(desc) {                     /// 'p:i'->"int-ptr", 'p:Filter'->"Filter-ptr"
        if (desc === undefined) {
            return 'unknown';
        }
        var head = descHead(desc);
        var tail = descTail(desc);
        if (head === 'S') {                                       /* struct value: category is the struct itself */
            return signatureCategoryForDesc(tail);
        }
        if (head === 'F') {                                       /* funcptr: the named type, or bare "funcptr" */
            return (tail !== undefined ? tail : 'funcptr');
        }
        if (tail === undefined) {
            return ((isStructAtom(head) || isFuncTypeAtom(head))
                    ? head : signatureParamCategory(head));
        }
        return signatureCategoryForDesc(tail) + '-ptr';           /* head is a pointer level */
    }

    /* full descriptor of a declared symbol: type char + optional element chain */
    function fullDescFor(type, elem) {
        return (elem !== undefined ? type + ':' + elem : type);
    }

    function signatureReturnCategory(type, known) {
        switch (type) {
            case 'i': return 'int';
            case 'f': return 'float';
            case 'p': return 'ptr';
            case 'F': return 'funcptr';
            case '?': return (known ? 'void' : 'unknown');
            default:  return 'unknown';
        }
    }

    function ensureFunctionSignature(name) {
        if (!name) {
            return undefined;
        }

        if (!symbols || !symbols.functions) {
            return undefined;
        }

        var entry = symbols.functions[name];
        if (!entry) {
            return undefined;
        }

        if (!entry.signature) {
            entry.signature = {};
        }

        return entry.signature;
    }

    function isConcreteType(type) {
        return type !== undefined && type !== '?';
    }

    function renderParamList(params) {
        if (!params || params.length === 0) {
            return '';
        }

        var parts = [];
        for (var idx = 0; idx < params.length; ++idx) {
            var param = params[idx] || {};
            var typeName = (param.type === 'S'
                    ? (param.struct || 'unknown')                 /* by-value struct param -> the struct name */
                    : (param.elem !== undefined
                        ? signatureCategoryForDesc(fullDescFor(param.type, param.elem))
                        : signatureParamCategory(param.type)));
            var name = param.name;
            if (!name) {
                name = 'arg' + idx;
            }
            parts.push(typeName + ' ' + name);
        }
        return parts.join(', ');
    }

    function renderTypeList(typeCodes) {
        if (!typeCodes || typeCodes.length === 0) {
            return '';
        }

        var parts = [];
        for (var i = 0; i < typeCodes.length; ++i) {
            parts.push(signatureParamCategory(typeCodes[i]));
        }
        return parts.join(', ');
    }

    function computeOrigin(sourceName, sourceCode, sourceOffset) {
        if (!sourceCode || sourceOffset == null) {
            return undefined;
        }

        var offset = sourceOffset;
        if (offset < 0) {
            return undefined;
        }
        if (offset > sourceCode.length) {
            offset = sourceCode.length;
        }

        var line = 1;
        var column = 1;
        for (var idx = 0; idx < offset; ++idx) {
            var ch = sourceCode.charAt(idx);
            if (ch === '\r') {
                if (idx + 1 < sourceCode.length && sourceCode.charAt(idx + 1) === '\n') {
                    idx += 1;
                }
                line += 1;
                column = 1;
                continue;
            }
            if (ch === '\n') {
                line += 1;
                column = 1;
                continue;
            }
            column += 1;
        }

        var origin = line + ':' + column;
        if (sourceName) {
            origin = sourceName + ':' + origin;
        }
        return origin;
    }

    function appendOrigin(comment, sourceName, sourceCode, sourceOffset) {
        if (!comment) {
            return comment;
        }

        var origin = computeOrigin(sourceName, sourceCode, sourceOffset);
        if (origin) {
            return comment + ' @ ' + origin;
        }
        return comment;
    }

    formatFunctionSignatureComment = function (name, signature, role,
                                                         sourceName, sourceCode, sourceOffset) {
        if (!signature) {
            return undefined;
        }

        var paramsText = renderParamList(signature.params);
        var hasReturn = (signature && signature.returns !== undefined);
        var returnType;
        if (signature.returnCount > 1) {                          /* multiple return values -> (t1, t2, ...) */
            var parts = [];
            for (var _rri = 0; _rri < signature.returnList.length; ++_rri) {
                var r = signature.returnList[_rri];
                parts.push(r.elem !== undefined
                        ? signatureCategoryForDesc(fullDescFor(r.type, r.elem))
                        : signatureParamCategory(r.type));
            }
            returnType = '(' + parts.join(', ') + ')';
        } else if (signature.returns === 'S') {                   /* by-value struct return -> the struct name */
            returnType = (signature.returnStruct || 'unknown');
        } else {
            returnType = (signature.returnElem !== undefined
                    ? signatureCategoryForDesc(fullDescFor(signature.returns, signature.returnElem))
                    : signatureReturnCategory(signature.returns, hasReturn));
        }
        var kind = (role ? role : 'func');
        if (exportNext) kind = 'export ' + kind;         /* Step 5: host-visible marker */

        var originName = (sourceName !== undefined ? sourceName : (signature ? signature.sourceName : undefined));
        var originCode = (sourceCode !== undefined ? sourceCode : (signature ? signature.sourceCode : undefined));
        var originOffset = (sourceOffset !== undefined ? sourceOffset : (signature ? signature.sourceOffset : undefined));

        return appendOrigin('signature ' + kind + ' ' + name + '(' + paramsText + ') -> ' + returnType,
                            originName, originCode, originOffset);
    };

    formatCallExpectationComment = function (name, signature, actualTypes, callResultType,
                                                      sourceName, sourceCode, sourceOffset) {
        var label = (name || 'function');
        var paramsText;
        var hasSignature = !!(signature && signature.params);

        if (hasSignature) {
            var extracted = [];
            for (var i = 0; i < signature.params.length; ++i) {
                var _p = signature.params[i];
                extracted.push(_p.type === 'S' ? (_p.struct || 'unknown')
                                               : signatureParamCategory(_p.type));
            }
            paramsText = extracted.join(', ');
        } else {
            paramsText = renderTypeList(actualTypes);
        }

        var returnCode = callResultType;
        var known = false;
        if (signature) {
            if (signature.returnResolved && signature.returns !== undefined) {
                returnCode = signature.returns;
                known = true;
            } else if (signature.expectedReturn !== undefined && signature.expectedReturn !== '?') {
                returnCode = signature.expectedReturn;
            } else if (signature.returns !== undefined) {
                returnCode = signature.returns;
                known = signature.returnResolved && signature.returns === '?';
            }
        }
        var returnType = (returnCode === 'S' && signature && signature.returnStruct
                ? signature.returnStruct                          /* by-value struct return -> the struct name */
                : signatureReturnCategory(returnCode, known));

        return appendOrigin('expects ' + label + '(' + paramsText + ') -> ' + returnType,
                            sourceName, sourceCode, sourceOffset);
    };

    updateCallExpectationComment = function (callInfo, callResultType) {
        if (!callInfo || callInfo.commentIndex === undefined || callInfo.commentIndex < 0) {
            return;
        }
        if (!metacode || callInfo.commentIndex >= metacode.length) {
            return;
        }

        var args = callInfo.commentArgs;
        if (!args) {
            return;
        }

        var refreshed = formatCallExpectationComment(
            args.name,
            args.signature,
            args.actualTypes,
            callResultType,
            args.sourceName,
            args.sourceCode,
            args.sourceOffset
        );

        if (!refreshed) {
            return;
        }

        var entry = metacode[callInfo.commentIndex];
        if (entry && entry.operator === ';') {
            entry.operands[0] = refreshed;
        }
    };

    expectFunctionReturnType = function (name, expectedType, sourceCode, sourceOffset) {
        if (!isConcreteType(expectedType)) {
            return;
        }

        var signature = ensureFunctionSignature(name);
        if (!signature) {
            return;
        }

        if (signature.expectedReturn === undefined) {
            signature.expectedReturn = expectedType;
        } else if (signature.expectedReturn !== expectedType) {
            typeError(
                'Conflicting return type expectations for ' + name + ' ({$type1} vs {$type2})',
                sourceCode,
                sourceOffset,
                expectedType,
                signature.expectedReturn,
                'E304'
            );
        }

        if (signature.returnResolved && isConcreteType(signature.returns)
            && signature.returns !== expectedType) {

            typeError(
                'Return type mismatch for ' + name + ' ({$type1} vs {$type2})',
                sourceCode,
                sourceOffset,
                expectedType,
                signature.returns,
                'E304'
            );
        }
    };

    resolveFunctionReturnType = function (name, actualType, sourceCode, sourceOffset) {
        var signature = ensureFunctionSignature(name);
        if (!signature) {
            return;
        }

        signature.returnResolved = true;
        signature.returns = actualType;

        if (signature.expectedReturn !== undefined && isConcreteType(signature.expectedReturn)
            && isConcreteType(actualType) && signature.expectedReturn !== actualType) {

            typeError(
                'Return type for ' + name + ' does not match previous uses ({$type1} vs {$type2})',
                sourceCode,
                sourceOffset,
                actualType,
                signature.expectedReturn,
                'E304'
            );
        }
    };

    emitFunctionSignature = function (name) {
        var entry = symbols.functions[name];
        if (!entry || entry.kind === 'FUNC') {
            return;
        }

        var comment = formatFunctionSignatureComment(name, entry.signature);

        var entryType = (entry.type !== undefined ? entry.type : 'U');

        declare('FUNC',
                         'functions',
                         name,
                         entryType,
                         true,
                         undefined,
                         entry.sourceCode,
                         entry.sourceOffset,
                         comment);
    };

    function signatureRoleForSection(section) {
        switch (section) {
            case 'CNST': return 'readonly';
            case 'TEMP': return 'temporary';
            default:     return 'global';
        }
    }

    formatGlobalSignatureComment = function (section, name, type, size, flavor,
                                                      sourceName, sourceCode, sourceOffset, elem) {
        if (!name) {
            return undefined;
        }

        var prefix = (exportNext ? 'export ' : '') + (flavor ? flavor + ' ' : '');

        if (type === 'A') {
            var extent = (size !== undefined ? '[' + size + ']' : '[]');
            var elemCategory = signatureCategoryForDesc(elem);    /* typed arrays advertise their element chain; */
                                                                  /* untyped arrays keep the `unknown` wildcard */
            return appendOrigin('signature ' + prefix + 'array ' + name + extent + ' : ' + elemCategory,
                                sourceName, sourceCode, sourceOffset);
        }

        return appendOrigin('signature ' + prefix + signatureRoleForSection(section) + ' ' +
                            name + ' : ' + signatureCategoryForDesc(fullDescFor(type, elem)),
                            sourceName, sourceCode, sourceOffset);
    };

    function emitStandaloneSignatureComment(comment) {
        if (!comment) {
            return;
        }

        if (typeof output === 'function') {
            output('; ' + comment);
        }
    }

    formatConstSignatureComment = function (name, type, sourceName, sourceCode, sourceOffset, elem) {
        if (!name) {
            return undefined;
        }

        return appendOrigin('signature ' + (exportNext ? 'export ' : '') + 'const ' + name + ' : '
                            + signatureCategoryForDesc(fullDescFor(type, elem)),
                            sourceName, sourceCode, sourceOffset);
    };

    /* 4  label & metacode helpers */
    newLabel = function (prefix) {
        var tag = (prefix === undefined ? '' : String(prefix));
        return '@.' + tag + (labelCounter++);
    };

    /* push a deep-cloned record into metacode */
    emitMeta = function (rec) {
        metacode.push(clone(metaSlot(rec)));
    };

    /* allocate new (empty) meta record, fill via makeMeta, then push */
    emit = function (op, type, op0, op1, op2) {
        var slot = {};                // fresh object
        makeMeta(slot, op, type, op0, op1, op2);  // user-supplied helper
        metacode.push(slot);
    };

    /* 5  portable replacement for ppeg.fail */
    fail = function (error, source, offset, code, hint) {
        function oneLine(s) { return replace(replace(replace(s,"\t",' '),"\r",' '),"\n",' '); }
        var message = bake(error);
        var hasSource = typeof source === 'string';
        var snippetSource = hasSource ? source : '';
        var snippetOffset = isFinite(offset) ? offset : 0;
        var before = oneLine(snippetSource.substr(snippetOffset - 8, 8));
        var after = oneLine(snippetSource.substr(snippetOffset, 40));
        var err = new Error(message + ' : ' + before + ' <!!!!> ' + after);
        err.impalaMessage = message;
        if (isFinite(offset)) {
            err.impalaOffset = offset;
        }
        err.impalaSnippetBefore = before;
        err.impalaSnippetAfter = after;
        if (code !== undefined) {
            err.impalaCode = code;                                /// stable diagnostic code, e.g. 'E201'
        }
        if (hint !== undefined) {
            err.impalaHint = hint;                                /// mechanical fix-it, rendered as a note line
        }
        throw err;
    };



    /* ---------------------------------------------------------
     *  Short-circuit / branch processing
     * --------------------------------------------------------- */
    processBranches = function () {
        var target      = { false: null, true: null }; // last FALSE / TRUE dest labels
        var targetCond  = null;                        // current branch condition (true / false)
        var currentGoto = null;                        // last unconditional goto
        var aliases     = {};                          // label alias map

        /* walk metacode bottom-to-top */
        for (var i = metacode.length - 1; i >= 0; --i) {
            var inst = metacode[i];

            switch (inst.operator) {

                /* ---  branch on TRUE / FALSE  --- */
                case '?->': {           /* created by AND/OR, e.g.   F->FALSE L1: */
                    targetCond         = inst.type; // boolean
                    var lbl            = inst.operands[0];
                    target[targetCond] = (lbl in aliases ? aliases[lbl] : lbl);
                    inst.operator      = null;      // remove
                    break;
                }

                case '<-?': {          /* unique FALSE/TRUE label, e.g.   FALSE L1: */
                    var lbl = inst.operands[0],
                        t   = inst.type;            // false / true
                    inst.type = null;               // will no longer be needed

                    if (target[t] != null) {        // label already chosen - make alias
                        aliases[lbl] = target[t];
                        inst.operator = (lbl[2] !== 'a' ? null : '<--'); // keep assert labels
                    } else {
                        target[t]    = lbl;
                        inst.operator = '<--';      // we retain the label
                    }
                    break;
                }

                /* ---  invert ( NOT )  --- */
                case '!':
                    var tmp    = target.false;
                    target.false = target.true;
                    target.true  = tmp;
                    targetCond   = !targetCond;
                    inst.operator = null;
                    break;

                /* comment - ignore */
                case ';':
                    break;

                /* ---  unconditional GOTO  --- */
                case '-->': {
                    var lbl   = inst.operands[0];
                    var final = (lbl in aliases ? aliases[lbl] : lbl);
                    target.false = target.true = currentGoto = inst.operands[0] = final;
                    break;
                }

                /* record label after an optimised goto */
                case '<--':
                    if (currentGoto != null) {
                        aliases[inst.operands[0]] = currentGoto;
                    }
                    break;

                /* ---  comparison ops  --- */
                case '<=': case '<': case '>=': case '>': case '!=': case '==': {
                    /* if we are targeting FALSE, invert comparison */
                    if (targetCond === false) inst.operator = '!' + inst.operator;

                    /* move operands left and patch jump target */
                    inst.operands[0] = inst.operands[1];
                    inst.operands[1] = inst.operands[2];
                    inst.operands[2] = target[targetCond];

                    /* reset all branch state */
                    target.false = target.true = currentGoto = null;
                    break;
                }

                /* ---  anything else breaks the chain  --- */
                default:
                    target.false = target.true = currentGoto = null;
            }
        }
    };

    /* ---------------------------------------------------------
     *  Pool / stock handling for transients    (‘%’, ‘<...>’)
     * --------------------------------------------------------- */

    /* assure no duplicates exist in a stock bucket */
    validateStock = function (cls) {
        var seen = {};
        var stk  = stock[cls];
        for (var i = 0; i < stk.length; ++i) {
            var tok = stk[i];
            assert(!seen[tok], "duplicate token in stock: " + tok);
            seen[tok] = true;
        }
        return true;
    };

    /* borrow one token from a stock bucket (or create a new one) */
    borrow = function (cls) {
        assert(validateStock(cls));

        var stk = stock[cls];
        if (stk.length) {
            return stk.pop();                      // reuse
        }

        /* otherwise mint a fresh id */
        if (cls === '%') {
            return '%' + (counters['%']++);
        }
        if (cls === '<') {
            var idx = counters['<']++;
            return '<' + String.fromCharCode('A'.charCodeAt(0) + idx) + '>';
        }
        throw new Error("unknown stock class " + cls);
    };

    /* smart borrow for CALL args - first free id in last consecutive run */
    borrowForCall = function () {
        /* same safety check the original did */
        assert(validateStock('%'));

        var stk = stock['%'];

        /* empty ⇒ mint a brand-new one */
        if (stk.length === 0) {
            return counters['%']++;
        }

        /* INVARIANT (R1): a call window grows UPWARD from its base, so the reused base must sit
           above every live transient - i.e. the free pool must reach the top (contain counters-1).
           This holds because every multi-slot window is released high-to-low (LIFO): ordinary
           expression temps, the argument window, the struct-return window (freeStructWindow) and
           the destructure output window (finishDestructure) all do. A low-to-high release would
           leave a freed hole below a live temp and break the reuse below - the fuzzer (seed 10024)
           caught exactly that when finishDestructure released low-to-high. Assert it so any future
           release-order regression fails loudly here instead of miscompiling. */
        var maxFree = -1;
        for (var _k = 0; _k < stk.length; ++_k) {
            var _v = parseInt(stk[_k].substr(1), 10);
            if (_v > maxFree) maxFree = _v;
        }
        assert(maxFree === counters['%'] - 1,
               'transient pool fragmented: a live temp sits above the free pool (release a window low-to-high?)');

        /* sort in-place on the numeric suffix, ascending           */
        stk.sort(function (a, b) {
            return parseInt(a.substr(1), 10) - parseInt(b.substr(1), 10);
        });

        /* walk backwards through the (now sorted) array,
           finding the *first* id in the last consecutive run       */
        var i = stk.length - 1,
            n = parseInt(stk[i].substr(1), 10);

        while (i >= 0 && stk[i] === '%' + n) {
            --i;
            --n;
        }
        ++i;               /* point at first element of the run  */
        ++n;               /* numeric id of the chosen transient */

        var chosen = n;
        stk.splice(i, 1);  /* remove from the pool                */

        /* Restore the stock order so ordinary borrow() calls reuse
           the most recently freed registers first, matching the
           original queue semantics. */
        stk.reverse();

        /* duplicate-check, like the original assert(validate...)    */
        assert(validateStock('%'));
        return chosen;
    };

    /* reserve transient %number (used to hold multi-return output slots) */
    claimSlot = function (number) {
        if (counters['%'] === number) {
            ++counters['%'];
        } else {
            assert(counters['%'] > number);
            var stk = stock['%'];
            for (var idx = stk.length - 1; idx >= 0 && stk[idx] !== '%' + number; --idx) {}
            assert(idx >= 0, "transient %" + number + " must exist in stock");
            stk.splice(idx, 1);
        }
    };

    /* put a token back into its stock bucket */
    function stockContains(stk, op) {
        for (var i = stk.length - 1; i >= 0; --i) {
            if (stk[i] === op) {
                return true;
            }
        }
        return false;
    }

    returnBack = function (op) {
        if (op == null) {
            return;
        }
        var c = op[0];

        /* ordinary transients / compile-time vars */
        if (c === '%' || c === '<') {
            var stk = stock[c];
            if (!stockContains(stk, op)) stk.push(op);   // avoid dupes
        }
        /* special case “...:<” suffix ---------------------------------
           original test:  op{len-4 : 2} == ':<'
           -> two chars beginning 4 from the end                       */
        else if (op.length >= 4 && op.substr(op.length - 4, 2) === ':<') {
            // recurse with everything from (len-3) to end
            returnBack(op.substr(op.length - 3));
        }
    };

    /* Align with the original PPEG helper while avoiding the reserved
       `return` identifier in generated JavaScript. */
    $$parser["return"] = returnBack;

    /* --------------------------------------------------------- *
     *  Debug helpers & meta-record construction / destruction   *
     * --------------------------------------------------------- */

    /* pretty-print one meta-instruction (only when it has op) */
    debugPrintMeta = function (m) {
        m = metaSlot(m);
        if (m && m.operator != null) {
            console.log(
                '{' + m.operator + '}(' + m.type + ') {'
                     + m.operands[0] + '} {' + m.operands[1]
                     + '} {' + m.operands[2] + '}'
            );
        }
    };

    /* lazily materialise a meta-record for any parse node */
    function metaSlot(node) {
        if (node == null || (typeof node !== 'object' && typeof node !== 'function')) {
            return { operator: undefined, type: undefined,
                     operands: [ undefined, undefined, undefined ] };
        }
        if (node.operands !== undefined) {
            if (!Array.isArray(node.operands)) {
                node.operands = [ undefined, undefined, undefined ];
            } else {
                while (node.operands.length < 3) {
                    node.operands.push(undefined);
                }
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'operator')) {
                node.operator = undefined;
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'type')) {
                node.type = undefined;
            }
            return node;
        }

        if (!Object.prototype.hasOwnProperty.call(node, '_')) {
            if (node.operands === undefined) {
                node.operands = [ undefined, undefined, undefined ];
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'operator')) {
                node.operator = undefined;
            }
            if (!Object.prototype.hasOwnProperty.call(node, 'type')) {
                node.type = undefined;
            }
            return node;
        }

        var slot = node._;
        if (!slot || slot.operands === undefined) {
            slot = { operator: undefined, type: undefined,
                     operands: [ undefined, undefined, undefined ] };
            node._ = slot;
        }
        return slot;
    }

    createParserContext = function () {
        return {
            _: { operator: undefined, type: undefined,
                 operands: [ undefined, undefined, undefined ] }
        };
    };

    /* overwrite the fields of an existing meta object */
    function normaliseVoid(value) {
        return value === null ? undefined : value;
    }

    makeMeta = function (rec, op, type, op0, op1, op2) {
        rec = metaSlot(rec);
        rec.operator  = normaliseVoid(op);
        rec.type      = normaliseVoid(type);
        rec.operands  = [
            normaliseVoid(op0),
            normaliseVoid(op1),
            normaliseVoid(op2)
        ];
        return rec;
    };

    /* release all three operands contained in a meta-record */
    releaseMeta = function (meta) {
        meta = metaSlot(meta);
        for (var i = 2; i >= 0; --i) {
            returnBack(meta.operands[i]);
        }
    };

    /* --------------------------------------------------------- *
     *  R-value helpers                                          *
     * --------------------------------------------------------- */

    /**
     * Convert an expression into an r-value, allocating a transient
     * when needed.  `classes` defaults to '#<&^$%'.
     */
    makeRValue = function (expr, classes) {
        classes = classes || '#<&^$%';

        expr = metaSlot(expr);

        var op   = expr.operator;
        var op1  = expr.operands[1];
        var op2  = expr.operands[2];
        var op1Prefix = (op1 ? op1[0] : '');
        var op2Prefix = (op2 ? op2[0] : '');

        /* already a simple l-value we can reuse? */
        if ((op === '=' || op === ':=') &&
            op1Prefix && span(op1Prefix, classes) === 1) {
            return op1;
        }

        /* otherwise evaluate into a transient */
        returnBack(op2);
        returnBack(op1);

        var cls = '%';              /* default stock   */
        var t   = op1Prefix + op2Prefix;

        if (t.length > 0 && span(t, '#<') === t.length && span('<', classes) === 1) {
            expr.operator = '<> ' + op;     /* compile-time op */
            cls = '<';
        }

        var tmp = borrow(cls);
        expr.operands[0] = tmp;

        emitMeta(expr);
        return tmp;
    };

    /**
     * Ensure an expression’s value ends up in the given
     * transient “%<number>”.
     */
    makeArgValue = function (expr, number) {
        expr = metaSlot(expr);

        var op   = expr.operator;
        var tgt  = '%' + number;
        var op1  = expr.operands[1];
        var op2  = expr.operands[2];

        /* already fine? */
        if ((op === '=' || op === ':=') && op1 === tgt) {
            return;
        }

        returnBack(op2);
        returnBack(op1);

        /* remove %<number> from the free list if present */
        if (counters['%'] === number) {
            ++counters['%'];
        } else {
            assert(counters['%'] > number);
            var stk = stock['%'];
            for (var idx = stk.length - 1;
                 idx >= 0 && stk[idx] !== tgt;
                 --idx) {}
            assert(idx >= 0, "transient " + tgt + " must exist in stock");
            stk.splice(idx, 1);
        }

        expr.operands[0] = tgt;
        emitMeta(expr);
    };

    /* --------------------------------------------------------- *
     *  Typed error helper                                       *
     * --------------------------------------------------------- */

    typeError = function (desc, source, offset, type1, type2, code, hint) {
        var message = replace(desc, '{$type1}',
                              VERBOSE_TYPES[type1]);
        if (type2 !== undefined) {
            message = replace(message, '{$type2}',
                               VERBOSE_TYPES[type2]);
        }
        fail(message, source, offset, code, hint);
    };

    /* --------------------------------------------------------- *
     *  Strict-expression helpers (Impala 2)                     *
     * --------------------------------------------------------- */

    strictError = function (message, source, offset, code, hint) {   /// error by default; warning under `--legacy`
        if (!legacyMode) {
            fail(message, source, offset, code, hint);
        } else if (typeof _hostOptions !== 'undefined' && _hostOptions != null
                && typeof _hostOptions.warn === 'function') {
            _hostOptions.warn(message, offset, code, hint);
        }
    };

    mixedBitwise = function (first, op, source, offset) {
        strictError("Mixed bitwise operators ('" + first + "' and '" + op
                + "') require parentheses", source, offset, 'E101',
                'add parentheses to keep the current meaning');
    };

    stampBitwise = function (rec, flag) {                /// mark whether `rec` folded top-level bitwise ops
        metaSlot(rec).bitwiseTop = flag;
    };

    checkCompMix = function (leftRec, rightRec, source, offset) {
        if (metaSlot(leftRec).bitwiseTop === true || metaSlot(rightRec).bitwiseTop === true) {
            strictError('Comparison mixed with bitwise operators requires parentheses',
                    source, offset, 'E102',
                    'add parentheses around the bitwise expression to keep the current meaning');
        }
    };

    /* --------------------------------------------------------- *
     *  Element-type descriptors (Impala 2 typed pointers/arrays) *
     *  A descriptor is a ':'-separated chain of atoms: 'i', 'f', *
     *  'p:i', 'p:p:i'. A terminal atom may be a struct NAME      *
     *  ('Filter', 'p:Filter') for typed struct pointers/arrays.  *
     * --------------------------------------------------------- */

    descHead = function (desc) {                         /// 'p:i' -> 'p'; 'Filter' -> 'Filter'
        if (desc === undefined) return undefined;
        var colon = desc.indexOf(':');
        return (colon === -1 ? desc : desc.substr(0, colon));
    };

    descTail = function (desc) {                         /// 'p:i' -> 'i'; 'i' -> undefined
        if (desc === undefined) return undefined;
        var colon = desc.indexOf(':');
        return (colon === -1 ? undefined : desc.substr(colon + 1));
    };

    isStructAtom = function (atom) {                     /// is `atom` a defined struct name?
        return atom !== undefined && structs
                && Object.prototype.hasOwnProperty.call(structs, atom);
    };

    isFuncTypeAtom = function (atom) {                   /// is `atom` a named funcptr type (Step 3)?
        return atom !== undefined && functypes
                && Object.prototype.hasOwnProperty.call(functypes, atom);
    };

    /* --------------------------------------------------------- *
     *  Named function-pointer types  (Impala 2 Step 3)          *
     * --------------------------------------------------------- */

    beginFuncType = function (name, sourceCode, sourceOffset) {
        if (isFuncTypeAtom(name)) {
            fail('Function type already defined: ' + name, sourceCode, sourceOffset, 'E440');
        }
        if (isStructAtom(name)) {
            fail('Type name already used by a struct: ' + name, sourceCode, sourceOffset, 'E440');
        }
        functypes[name] = { params: [], returnList: [], returnCount: 0,
                returns: '?', returnElem: undefined, returnStruct: undefined, returnWords: 0,
                complete: false, sourceCode: sourceCode, sourceOffset: sourceOffset,
                sourceName: sourceName };
    };

    addFuncTypeParam = function (name, type, elem, struct, words, pname) {
        functypes[name].params.push({ type: type, elem: elem,
                struct: struct, words: words, name: pname });
    };

    addFuncTypeReturn = function (name, type, elem, struct, words, sourceCode, sourceOffset) {
        var ft = functypes[name];
        if (type === 'S' && ft.returnList.length > 0) {
            fail('A by-value struct return must be the only return value',
                    sourceCode, sourceOffset, 'E430');
        }
        if (type !== 'S' && ft.returnList.length > 0 && ft.returnList[0].type === 'S') {
            fail('A by-value struct return must be the only return value',
                    sourceCode, sourceOffset, 'E430');
        }
        ft.returnList.push({ type: type, elem: elem, struct: struct, words: words });
    };

    endFuncType = function (name) {
        var ft = functypes[name];
        ft.complete = true;
        ft.returnCount = ft.returnList.length;
        if (ft.returnCount > 0) {
            ft.returns      = ft.returnList[0].type;
            ft.returnElem   = ft.returnList[0].elem;
            ft.returnStruct = ft.returnList[0].struct;
            var rw = 0;
            for (var i = 0; i < ft.returnList.length; ++i) {
                rw += (ft.returnList[i].type === 'S' ? ft.returnList[i].words : 1);
            }
            ft.returnWords = rw;
        } else {
            ft.returns = '?';
            ft.returnWords = 0;
        }
    };

    /* Does a concrete function signature satisfy a named funcptr type? Compares arity, each
       parameter's type (+ struct / pointer-element), and the return shape. */
    funcTypeMatches = function (ftName, signature) {
        var ft = functypes[ftName];
        if (!ft || !signature) return true;                       /* unknown -> don't over-report */
        var fp = ft.params || [], sp = signature.params || [];
        if (fp.length !== sp.length) return false;
        for (var i = 0; i < fp.length; ++i) {
            if (fp[i].type !== sp[i].type) return false;
            if (fp[i].type === 'S' && fp[i].struct !== sp[i].struct) return false;
            if (fp[i].type === 'p' && fp[i].elem !== undefined && sp[i].elem !== undefined
                    && fp[i].elem !== sp[i].elem) return false;
        }
        var sc = (signature.returnCount === undefined ? (signature.returns === '?' ? 0 : 1)
                                                      : signature.returnCount);
        if ((ft.returnCount || 0) !== sc) return false;
        if (ft.returns !== signature.returns) return false;
        if (ft.returns === 'S' && ft.returnStruct !== signature.returnStruct) return false;
        return true;
    };

    elemVerbose = function (desc) {                      /// 'p:i' -> "int pointer"; 'p:Filter' -> "Filter pointer"
        if (desc === undefined) return 'untyped';
        var head = descHead(desc);
        var tail = descTail(desc);
        if (head === 'F') {                                       /* funcptr: named type or bare "funcptr" */
            return (tail !== undefined ? tail : 'funcptr');
        }
        if (tail === undefined) {
            return ((isStructAtom(head) || isFuncTypeAtom(head))
                    ? head : VERBOSE_TYPES[head]);
        }
        return elemVerbose(tail) + ' pointer';           /* head is a 'p' level */
    };

    setElem = function (rec, elem) {                     /// stamp element type on a meta record
        metaSlot(rec).elem = elem;
    };

    /* --------------------------------------------------------- *
     *  Struct layouts (Impala 2 Step 2)                          *
     * --------------------------------------------------------- */

    structWords = function (name) {                      /// total word size of a defined struct
        var s = structs[name];
        return (s && s.complete ? s.words : undefined);
    };

    fieldWords = function (field) {                      /// words occupied by one field
        if (field.type === 'S') {
            return structWords(field.struct);
        }
        if (field.type === 'A') {
            var per = (isStructAtom(field.elem)
                    ? structWords(field.elem) : 1);
            return field.size * per;
        }
        return 1;                                                 /* scalar i/f/p/F */
    };

    beginStruct = function (name, sourceCode, sourceOffset) {
        var prev = structs[name];
        if (prev && prev.complete) {
            fail('Struct already defined: ' + name, sourceCode, sourceOffset, 'E410');
        }
        structs[name] = { fields: [], words: 0, complete: false,
                sourceCode: sourceCode, sourceOffset: sourceOffset };
    };

    endStruct = function (name) {
        structs[name].complete = true;
    };

    externStruct = function (name, sourceCode, sourceOffset) {   /// forward / opaque declaration
        if (!structs[name]) {
            structs[name] = { fields: [], words: 0, complete: false,
                    sourceCode: sourceCode, sourceOffset: sourceOffset };
        }
    };

    addStructField = function (name, field, sourceCode, sourceOffset) {
        var s = structs[name];
        for (var i = 0; i < s.fields.length; ++i) {
            if (s.fields[i].name === field.name) {
                fail('Duplicate field ' + field.name + ' in struct ' + name,
                        sourceCode, sourceOffset, 'E411');
            }
        }
        if (field.type === 'S' && structWords(field.struct) === undefined) {
            fail('Field ' + field.name + ' has incomplete struct type ' + field.struct
                    + ' (define it, or use a pointer)', sourceCode, sourceOffset, 'E412');
        }
        field.offset = s.words;
        field.words = fieldWords(field);
        s.fields.push(field);
        s.words += field.words;
    };

    /* Walk a struct layout against a brace-tree of constants, producing the flat list of
       constant operands (field order); trailing/omitted slots zero-fill. Recurses for
       nested struct and array fields. `items` is an array of { op, type } | { braced:[...] }. */
    buildStructInit = function (structName, items, out, sourceCode, sourceOffset) {
        var fields = structs[structName].fields;
        for (var fi = 0; fi < fields.length; ++fi) {
            var f = fields[fi];
            var item = (items && fi < items.length) ? items[fi] : undefined;
            if (f.type === 'S') {
                buildStructInit(f.struct, (item && item.braced) || [], out, sourceCode, sourceOffset);
            } else if (f.type === 'A') {
                var arr = (item && item.braced) || [];
                var structEl = isStructAtom(f.elem);
                for (var e = 0; e < f.size; ++e) {
                    var ev = (e < arr.length) ? arr[e] : undefined;
                    if (structEl) {
                        buildStructInit(f.elem, (ev && ev.braced) || [], out, sourceCode, sourceOffset);
                    } else {
                        pushInitScalar(out, ev, f.elem, f.name, sourceCode, sourceOffset);
                    }
                }
            } else {
                pushInitScalar(out, item, f.type, f.name, sourceCode, sourceOffset);
            }
        }
    };

    pushInitScalar = function (out, item, type, fieldName, sourceCode, sourceOffset) {
        if (item === undefined) {
            out.push(ZEROES[type]);                      /* omitted -> zero */
            return;
        }
        if (item.braced !== undefined) {
            fail('Too many braces in initializer for field ' + fieldName,
                    sourceCode, sourceOffset, 'E422');
        }
        if (item.type !== type) {
            typeError('Initializer type mismatch for field ' + fieldName + ' ({$type1} vs {$type2})',
                    sourceCode, sourceOffset, item.type, type, 'E422');
        }
        out.push(item.op);
    };

    /* emit a flat constant list as one or more DATA rows (mirrors InitList chunking) */
    emitInitData = function (ops, sourceCode, sourceOffset) {
        var line = '';
        for (var i = 0; i < ops.length; ++i) {
            if (line !== '' && (line + ' ' + ops[i]).length >= 55) {
                declare('DATA', 'globals', undefined, 'i', true, line, sourceCode, sourceOffset);
                line = '';
            }
            line += (line === '' ? '' : ' ') + ops[i];
        }
        if (line !== '') {
            declare('DATA', 'globals', undefined, 'i', true, line, sourceCode, sourceOffset);
        }
    };

    /* record one `returns` declarator on a function entry (Step 4: multiple return values,
       slice 2.5: by-value struct returns) */
    addReturn = function (entry, rawName, type, elem, size, sourceCode, sourceOffset, struct, words) {
        if (!entry.pendingReturns) {
            entry.pendingReturns = [];
        }
        if (type === 'S' && entry.pendingReturns.length > 0) {
            fail('A by-value struct return must be the only return value',
                    sourceCode, sourceOffset, 'E430');
        }
        if (type !== 'S' && entry.pendingReturns.length > 0
                && entry.pendingReturns[0].type === 'S') {
            fail('A by-value struct return must be the only return value',
                    sourceCode, sourceOffset, 'E430');
        }
        entry.pendingReturns.push({
            name: '$' + rawName,
            rawName: rawName,
            type: type,
            elem: elem,
            size: (size !== undefined ? '*' + size : undefined),
            struct: struct,
            words: words,
            sourceCode: sourceCode,
            sourceOffset: sourceOffset
        });
    };

    findField = function (structName, fieldName) {
        var s = structs[structName];
        if (!s) return undefined;
        for (var i = 0; i < s.fields.length; ++i) {
            if (s.fields[i].name === fieldName) return s.fields[i];
        }
        return undefined;
    };

    /* A "place" names a struct sitting at (base + compile-time offset). Struct-typed
       expressions are compile-time places; only terminal scalar fields emit instructions,
       so struct values never enter the one-word expression temporaries. */
    setPlace = function (rec, baseKind, base, offset, structName) {
        var slot = metaSlot(rec);
        slot.operator  = '@place';
        slot.type      = 'S';
        slot.operands  = [ undefined, undefined, undefined ];
        slot.place     = true;
        slot.baseKind  = baseKind;                                /* 'local' | 'pointer' */
        slot.base      = base;
        slot.placeOff  = offset;
        slot.struct    = structName;
        slot.elem      = structName;
    };

    /* structPtr[i]  ->  a struct place at ptr + i*sizeof (struct arrays decay to struct pointers) */
    structSubscript = function (x, idx, sourceCode, sourceOffset) {
        x = metaSlot(x);
        idx = metaSlot(idx);
        var structName = x.elem;
        var sz = structWords(structName);
        var basePtr = makeRValue(x);
        var idxRV = makeRValue(idx);

        if (/^#[0-9]+$/.test(idxRV)) {                            /* constant index -> fold into the place offset */
            var off = parseInt(idxRV.substr(1), 10) * sz;
            /* a global-address base (&name) stays in global memory (=* / &name:off);
               a runtime pointer base uses PEEK/POKE. */
            setPlace(x, (basePtr[0] === '&' ? 'globalAddr' : 'pointer'), basePtr, off, structName);
            returnBack(idxRV);
            return;
        }

        var addr = borrow('%');                          /* dynamic index -> ptr + i*sizeof (a runtime pointer) */
        if (sz === 1) {
            emit('+', 'p', addr, basePtr, idxRV);
        } else {
            var scaled = borrow('%');
            emit('*', 'i', scaled, idxRV, '#' + sz);
            emit('+', 'p', addr, basePtr, scaled);
            returnBack(scaled);
        }
        returnBack(idxRV);
        returnBack(basePtr);
        setPlace(x, 'pointer', addr, 0, structName);
    };

    /* materialize a place's address into a pointer operand (borrows a temp for locals) */
    placeAddress = function (place) {
        place = metaSlot(place);
        if (place.baseKind === 'globalAddr') {                    /* global: &name(:off) is directly addressable */
            return place.base + (place.placeOff ? ':' + place.placeOff : '');
        }
        if (place.baseKind === 'pointer') {
            if (place.placeOff === 0) {
                return place.base;
            }
            var t = borrow('%');
            emit('+', 'p', t, place.base, '#' + place.placeOff);
            return t;
        }
        var a = borrow('%');                             /* local: ADRL then optional add */
        emit('=&', 'p', a, place.base, '*' + structWords(place.struct));
        if (place.placeOff !== 0) {
            emit('+', 'p', a, a, '#' + place.placeOff);
        }
        return a;
    };

    /* by-value struct argument: reserve `words` window slots at %winSlot and COPY the
       struct value in (experiment-verified: ADRL the window region, ADRL the source, COPY). */
    copyStructArg = function (argMeta, winSlot, words) {
        argMeta = metaSlot(argMeta);
        /* A struct-return result already sits in transient window slots. Nested calls slide
           their windows so the inner result lands exactly where this argument belongs - then
           there is nothing to copy (and self-copy would be undefined behaviour): adopt it. */
        if (argMeta.winBase === winSlot && argMeta.winWords === words) {
            returnBack(argMeta.base);                    /* free the window-address pointer temp */
            argMeta.winBase = undefined;                          /* slots are now the argument window (freed by the call) */
            argMeta.winWords = undefined;
            return;
        }
        for (var k = 0; k < words; ++k) {                         /* reserve the window slots */
            claimSlot(winSlot + k);
        }
        var dst = borrow('%');
        emit('=&', 'p', dst, '%' + winSlot, '*' + words);   /* ADRL address of the window region */
        var src = placeAddress(argMeta);                    /* address of the source struct */
        emit('copy', '?', dst, src, '*' + words);           /* COPY dst src *words */
        returnBack(src);
        returnBack(dst);
        freeStructWindow(argMeta);                          /* if the source was a struct-return window, release it */
    };

    /* A struct-return call result lives in transient output-window slots that must be freed
       once the value has been consumed (copied out / read). Named-place operands (locals,
       globals, pointers) carry no window and are ignored. */
    freeStructWindow = function (place) {
        place = metaSlot(place);
        if (place.winBase !== undefined) {
            for (var i = place.winWords - 1; i >= 0; --i) {
                returnBack('%' + (place.winBase + i));
            }
            place.winBase = undefined;
            place.winWords = undefined;
        }
    };

    /* fp->field, place.field, nested chains - Step 2 slices 1-2 */
    fieldAccess = function (x, fieldName, arrow, sourceCode, sourceOffset) {
        x = metaSlot(x);

        var bk, base, off, structName;
        if (x.place) {
            if (x.winBase !== undefined) {
                fail("Cannot access a field directly on a returned struct value",
                        sourceCode, sourceOffset, 'E423',
                        'assign the call result to a local first, then read its fields');
            }
            if (arrow) {
                fail("Use '.' - this is a struct value, not a pointer", sourceCode,
                        sourceOffset, 'E416', 'write ' + fieldName + ' as .' + fieldName);
            }
            bk = x.baseKind; base = x.base; off = x.placeOff; structName = x.struct;
        } else if (x.type === 'p' && isStructAtom(x.elem)) {
            if (!arrow) {
                fail("Use '->' to access a field through a pointer", sourceCode,
                        sourceOffset, 'E416', 'write ' + fieldName + ' as ->' + fieldName);
            }
            bk = 'pointer'; base = makeRValue(x); off = 0; structName = x.elem;
        } else {
            fail("Field access requires a struct" + (arrow ? ' pointer' : ''),
                    sourceCode, sourceOffset, 'E415');
        }

        var field = findField(structName, fieldName);
        if (!field) {
            fail('Struct ' + structName + ' has no field ' + fieldName,
                    sourceCode, sourceOffset, 'E417');
        }
        var total = off + field.offset;

        if (field.type === 'S') {                                 /* nested inline struct -> another place */
            setPlace(x, bk, base, total, field.struct);
            return;
        }
        if (field.type === 'A') {                                 /* array field decays to a typed pointer at base+offset */
            if (bk === 'globalAddr') {
                makeMeta(x, ':=', 'p', undefined, base + (total ? ':' + total : ''), undefined);
            } else if (bk === 'pointer') {
                if (total === 0) {
                    makeMeta(x, ':=', 'p', undefined, base, undefined);
                } else {
                    var pt = borrow('%');
                    emit('+', 'p', pt, base, '#' + total);
                    makeMeta(x, ':=', 'p', undefined, pt, undefined);
                }
            } else {                                              /* local: ADRL then optional add */
                var lt = borrow('%');
                emit('=&', 'p', lt, base, '*' + structWords(structName));
                if (total !== 0) {
                    emit('+', 'p', lt, lt, '#' + total);
                }
                makeMeta(x, ':=', 'p', undefined, lt, undefined);
            }
            x.place = false;
            x.type  = 'p';
            setElem(x, field.elem);
            return;
        }

        /* terminal scalar field */
        if (bk === 'local') {
            makeMeta(x, '=', field.type, undefined, base + ':' + total, undefined);
        } else if (bk === 'globalAddr') {                         /* &name:off in global memory */
            makeMeta(x, '=*', field.type, undefined, base + ':' + total, undefined);
        } else {                                                  /* pointer base: PEEK/POKE base #offset */
            makeMeta(x, '=[]', field.type, null, base, '#' + total);
        }
        x.place = false;
        x.type  = field.type;
        x.elem  = field.elem;
    };

    checkPtrAssign = function (leftx, rightx, sourceCode, sourceOffset) {
        if (leftx.type === 'p' && leftx.elem !== undefined) {     /* typed pointer target: assume loudly */
            if (rightx.operands && rightx.operands[1] === '&NULL'
                    && rightx.operands[2] === undefined) {
                return;                                           /* null is assignable to any pointer type */
            }
            if (rightx.elem !== leftx.elem) {
                fail('Pointer element type mismatch (expected '
                        + elemVerbose(leftx.elem) + ' elements, got '
                        + elemVerbose(rightx.elem) + ' elements)',
                        sourceCode, sourceOffset, 'E201',
                        'use a cast: (' + elemVerbose(leftx.elem) + ' pointer)');
            }
        }
        if (leftx.type === 'F' && isFuncTypeAtom(leftx.elem)) {   /* named funcptr type target: check the signature */
            var rop = rightx.operands && rightx.operands[1];
            if (rop === '&NULL' && rightx.operands[2] === undefined) {
                return;                                           /* nullfunc is assignable to any funcptr type */
            }
            if (rop && (rop[0] === '&' || rop[0] === '^')) {      /* a direct function reference */
                var fe = symbols.functions[rop.substr(1)];
                if (fe && fe.signature && !funcTypeMatches(leftx.elem, fe.signature)) {
                    fail('Function ' + rop.substr(1) + " does not match funcptr type '"
                            + leftx.elem + "'", sourceCode, sourceOffset, 'E441',
                            'check the parameter and return types against ' + leftx.elem);
                }
            } else if (rightx.type === 'F' && rightx.elem !== undefined
                    && isFuncTypeAtom(rightx.elem) && rightx.elem !== leftx.elem) {
                fail("Funcptr type mismatch (expected '" + leftx.elem + "', got '"
                        + rightx.elem + "')", sourceCode, sourceOffset, 'E441',
                        'cast explicitly if this is intentional');
            }
        }
    };

    /* --------------------------------------------------------- *
     *  Binary operations ( + - * / [] etc. )                    *
     * --------------------------------------------------------- */
    binaryOp = function (operator, leftx, rightx,
                                  sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var lelem = leftx.elem;                                   /* element type of the base (Impala 2) */

        /* validate operand-type combination */
        var sig = operator + leftx.type + rightx.type;
        var tp  = SUPPORTED_OPS[sig];
        if (tp === undefined) {
            typeError(
                'Invalid types ({$type1} and {$type2})',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            , 'E301');
        }

        /* special treatment for indexed “=[]” form */
        if (operator === '=[]') {
            var op1 = leftx.operands[1];     /* base pointer/address */

            if (leftx.operator === ':=' && op1[0] === '&') {

                var op2 = makeRValue(rightx);

                if (op2[0] === '#') {
                    makeMeta(
                        leftx, '=*', tp, null,
                        op1 + ':' + op2.substr(1), null
                    );
                } else if (op2[0] === '<') {
                    makeMeta(leftx, '=*', tp, null,
                                      op1 + ':' + op2, null);
                } else {
                    makeMeta(leftx, '=[]', tp, null, op1, op2);
                }

            } else if (leftx.operator === '=&') {
                assert(op1[0] === '$', "=& expects local '$'");

                var op2b = makeRValue(rightx);

                if (op2b[0] === '#') {
                    makeMeta(leftx, '=', tp, null,
                                      op1 + ':' + op2b.substr(1), null);
                } else if (op2b[0] === '<') {
                    makeMeta(leftx, '=', tp, null,
                                      op1 + ':' + op2b, null);
                } else {
                    makeMeta(leftx, '=[]$', tp, null, op1, op2b);
                }

            } else {
                /* general indexed read */
                makeMeta(leftx, operator, tp, null,
                                  makeRValue(leftx),
                                  makeRValue(rightx));
            }

        } else {

            /* pointer-difference special-case “d” */
            if (operator === '-' && rightx.type === 'p') {
                operator = 'd';
            }

            makeMeta(
                leftx, operator, tp, null,
                makeRValue(leftx),
                makeRValue(rightx)
            );
        }

        /* element-type propagation (Impala 2) */
        if (operator === '=[]' && lelem !== undefined) {
            var _eh = descHead(lelem);                   /* typed element read: no cast needed */
            var _et = descTail(lelem);
            if (_et === undefined && isFuncTypeAtom(_eh)) {
                leftx.type = 'F';                                 /* funcptr-array element carries its funcptr type */
                leftx.elem = _eh;
            } else {
                leftx.type = _eh;
                leftx.elem = _et;
            }
        } else if ((operator === '+' || operator === '-') && leftx.type === 'p') {
            leftx.elem = lelem;                                   /* pointer arithmetic preserves element type */
        } else {
            leftx.elem = undefined;
        }
    };


    /* --------------------------------------------------------- *
     *  Multiplication / division with special int-to-float case *
     * --------------------------------------------------------- */
    mulDivOp = function (operator, leftx, rightx,
                                  sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var sig = operator + leftx.type + rightx.type;
        var tp  = SUPPORTED_OPS[sig];
        if (tp === undefined) {
            typeError('Invalid types ({$type1} and {$type2})',
                               sourceCode, sourceOffset,
                               leftx.type, rightx.type, 'E301');
        }

        /* detect (itof X) * 1.0 -> itof */
        if (operator === '*' && leftx.operator === '=itof'
            && leftx.operands[2] === '#1.0') {

            var rightOp1Prefix = (rightx.operands[1] ? rightx.operands[1][0] : '');
            var rightOp2Prefix = (rightx.operands[2] ? rightx.operands[2][0] : '');
            var t  = rightOp1Prefix + rightOp2Prefix;
            var ok = (t.length > 0 && span(t, '#<') === t.length);

            if (ok) {
                makeMeta(
                    leftx, '=itof', 'f', null,
                    leftx.operands[1],
                    makeRValue(rightx)
                );
                leftx.elem = undefined;
                return;
            }
        }

        /* mirror case: right side is itof */
        if (operator === '*' && rightx.operator === '=itof'
            && rightx.operands[2] === '#1.0') {

            var leftOp1Prefix = (leftx.operands[1] ? leftx.operands[1][0] : '');
            var leftOp2Prefix = (leftx.operands[2] ? leftx.operands[2][0] : '');
            var t2 = leftOp1Prefix + leftOp2Prefix;
            var ok2 = (t2.length > 0 && span(t2, '#<') === t2.length);

            if (ok2) {
                makeMeta(
                    leftx, '=itof', 'f', null,
                    rightx.operands[1],
                    makeRValue(leftx)
                );
                leftx.elem = undefined;
                return;
            }
        }

        /* default multiply / divide / mod path */
        makeMeta(
            leftx, operator, tp, null,
            makeRValue(leftx),
            makeRValue(rightx)
        );
        leftx.elem = undefined;
    };


    /* --------------------------------------------------------- *
     *  Assignment helper                                        *
     * --------------------------------------------------------- */
    assign = function (x, leftx, rightx,
                                sourceCode, sourceOffset) {

        x      = metaSlot(x);
        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        /* whole-struct assignment: one COPY *sizeof, statement value is the dest place */
        if (leftx.type === 'S' || rightx.type === 'S') {
            if (leftx.type !== 'S' || rightx.type !== 'S') {
                fail('Whole-struct assignment needs a struct value on both sides',
                        sourceCode, sourceOffset, 'E420');
            }
            if (leftx.struct !== rightx.struct) {
                fail('Struct type mismatch in assignment (' + leftx.struct + ' = '
                        + rightx.struct + ')', sourceCode, sourceOffset, 'E420');
            }
            var words   = structWords(leftx.struct);
            var savedBK = leftx.baseKind, savedBase = leftx.base,
                savedOff = leftx.placeOff, savedStruct = leftx.struct;
            var dst = placeAddress(leftx);
            var src = placeAddress(rightx);
            makeMeta(x, 'copy', '?', dst, src, '*' + words);
            emitMeta(x);
            returnBack(src);
            returnBack(dst);
            freeStructWindow(rightx);                    /* release a struct-return window once copied out */
            setPlace(x, savedBK, savedBase, savedOff, savedStruct);
            return;
        }

        if (!leftx || leftx.operator === undefined) {
            throw new Error('JSPEG meta missing for assignment: ' + JSON.stringify(leftx));
        }

        var lop   = leftx.operator;
        var keep  = 2;          /* operand index to keep for r-value */

        if (leftx.type !== '?' && rightx.type === '?' && rightx.callInfo && rightx.callInfo.name) {
            expectFunctionReturnType(rightx.callInfo.name, leftx.type, sourceCode, sourceOffset);
            rightx.type = leftx.type;
            updateCallExpectationComment(rightx.callInfo, leftx.type);
        }

        if (leftx.type !== '?' && rightx.type !== '?' && leftx.type !== rightx.type) {
            typeError(
                'Incompatible types for assignment ({$type1} = {$type2})',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            , 'E303');
        }

        checkPtrAssign(leftx, rightx, sourceCode, sourceOffset);

        /* fast path: constant expression on RHS */
        var op1 = rightx.operands[1];
        var op2 = rightx.operands[2];
        var constPair  = (op1 ? op1[0] : '') +
                         (op2 ? op2[0] : '');
        var rhsConst   = (span(constPair, '#<') === constPair.length);

        if (lop === '=' && rhsConst) {

            makeMeta(
                x, ':=', rightx.type,
                leftx.operands[1],
                makeRValue(rightx),
                null
            );
            keep = 1;

        } else if (lop === '=') {

            makeMeta(
                x, rightx.operator, rightx.type,
                leftx.operands[1],
                rightx.operands[1],
                rightx.operands[2]
            );
            keep = ((x.operator === '=' || x.operator === ':=') ? 1 : 0);

        } else if (lop === '=*') {

            makeMeta(
                x, '*=', rightx.type,
                leftx.operands[1],
                makeRValue(rightx),
                null
            );
            keep = 1;

        } else if (lop === '=[]') {

            makeMeta(
                x, '[]=', rightx.type,
                leftx.operands[1],
                leftx.operands[2],
                makeRValue(rightx)
            );

        } else if (lop === '=[]$') {

            makeMeta(
                x, '[]$=', rightx.type,
                leftx.operands[1],
                leftx.operands[2],
                makeRValue(rightx)
            );

        } else {
            fail("Invalid lvalue", sourceCode, sourceOffset, 'E404');
        }

        /* push the instruction just built */
        emitMeta(x);

        /* release all temporaries except the one we keep */
        for (var i = 2; i >= 0; --i) {
            if (i !== keep) {
                returnBack(x.operands[i]);
            }
        }

        /* finally generate r-value of assignment */
        makeMeta(
            x, '=', x.type,
            null,
            x.operands[keep],
            null
        );
    };

    /* -----------------------------------------------------------
     *  Multi-return destructuring   a, b = f(...)
     *  callMeta is the finished FuncCall meta whose output window
     *  lives in %multiBase .. %multiBase+multiCount-1.
     * -------------------------------------------------------- */
    finishDestructure = function (callMeta, targets, sourceCode, sourceOffset) {
        callMeta = metaSlot(callMeta);
        var base  = callMeta.multiBase;
        var count = callMeta.multiCount;
        var list  = callMeta.multiReturnList || [];

        if (base === undefined || count === undefined || count < 2) {
            fail('Left side lists ' + targets.length
                    + ' targets but the right side is not a multi-value function call',
                    sourceCode, sourceOffset, 'E432');
        }
        if (targets.length !== count) {
            fail('Destructuring assignment expects ' + count
                    + ' targets, got ' + targets.length,
                    sourceCode, sourceOffset, 'E433');
        }

        for (var i = 0; i < count; ++i) {
            var slot = '%' + (base + i);
            var t    = targets[i];
            var ret  = list[i] || {};

            if (t.name === '_') {                     /* discard: slot freed in the release pass below */
                continue;
            }

            var lv = { operator: undefined, type: '?',
                       operands: [ undefined, undefined, undefined ] };
            lookup(lv, t.name, t.isGlobal, t.code, t.offset);

            var rv = { operator: ':=', type: (ret.type || '?'),
                       operands: [ undefined, slot, undefined ] };
            setElem(rv, ret.elem);

            var res = { operator: undefined, type: '?',
                        operands: [ undefined, undefined, undefined ] };
            assign(res, lv, rv, sourceCode, sourceOffset);  /* reads %slot; leaves it allocated */
        }

        /* Release the output-window slots high-to-low, the LIFO order every other multi-slot
           window (arg window, struct-return window) already uses. Freeing low-to-high left a
           hole below a live temp, breaking borrowForCall's reuse (invariant R2 / fuzzer 10024). */
        for (var j = count - 1; j >= 0; --j) {
            returnBack('%' + (base + j));
        }
    };

    /* -----------------------------------------------------------
     *  Unary helpers  (dereference, reference, -, ~, abs/floor,
     *                  int↔float conversions)
     * -------------------------------------------------------- */

    /* *expr  or  [] dereference handling */
    dereference = function (operator, expr, sourceCode, sourceOffset) {
        expr = metaSlot(expr);
        if (expr.operator === '+') {
            /*  &a + i   ->   PEEK (&a , i)  */
            expr.operator = '=[]';
        } else if (expr.operator === '-' && expr.operands[2] &&
                   expr.operands[2][0] === '#') {
            /*  &a - #n  where n is const -> adjust to negative literal */
            expr.operator = '=[]';
            var num = parseFloat(expr.operands[2].substr(1));   // strip leading '#'
            expr.operands[2] = '#'+(-num);
        } else {
            /* generic “*expr” */
            makeMeta(
                expr, operator, '?',
                undefined,
                makeRValue(expr),
                undefined
            );
        }
    };

    /* & (address-of) operator handling */
    reference = function (operator, expr, sourceCode, sourceOffset) {

        expr = metaSlot(expr);

        if (expr.operator === '=') {                 // variable
            assert(expr.operands[2] === undefined,
                   "expr.operands[2] must be void for '=' lvalue");
            expr.operator  = '=&';
            expr.operands[2] = '*0';

        } else if (expr.operator === '=*' || expr.operator === ':=*') {
            assert(expr.operands[2] === undefined,
                   "expr.operands[2] must be void for '=*' lvalue");
            expr.operator = ':=';                    // treat as plain r-value

        } else if (expr.operator === '=[]') {        // array element
            expr.operator = '+';                     // &a[i]  ->  &a + i

        } else if (expr.operator === '=[]$') {       // local array element
            expr.operator = '=&';
            var index = expr.operands[2];            // save index before clobber
            expr.operands[2] = '*0';
            makeMeta(
                expr, '+', 'p',
                undefined,
                makeRValue(expr),           // &base
                index                                // + offset
            );

        } else {
            fail("Invalid lvalue", sourceCode, sourceOffset, 'E404');
        }
    };

    /* unary minus (integer/float) */
    minus = function (operator, expr/*, src, off*/) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '-', undefined,
            undefined,
            ZEROES[ expr.type ],            // 0  of same type
            makeRValue(expr)
        );
    };

    /* bit-wise NOT / logical NOT  (~expr) */
    not = function (operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '^', undefined,
            undefined,
            makeRValue(expr),
            '#-1'                                    // XOR with -1
        );
    };

    /* ABS or FLOOR (unary) - operator is already '=abs' or '=floor' */
    absFloor = function (operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, operator, undefined,
            undefined,
            makeRValue(expr),
            undefined
        );
    };

    /* int -> float */
    intToFloatConvert = function (operator, expr) {
        expr = metaSlot(expr);
        makeMeta(
            expr, '=itof', undefined,
            undefined,
            makeRValue(expr),
            '#1.0'
        );
    };

    /* float -> int, with constant-fold special-case */
    floatToIntConvert = function (operator, expr) {

        expr = metaSlot(expr);

        var op1 = expr.operands[1],
            op2 = expr.operands[2];

        /* expr is  (const|<tmp>) * #1.0  pattern */
        if (expr.operator === '*' && op2 && span(op2[0], '#<') === 1) {
            makeMeta(expr, '=ftoi', undefined, undefined, op1, op2);

        } else if (expr.operator === '*' && op1 && span(op1[0], '#<') === 1) {
            makeMeta(expr, '=ftoi', undefined, undefined, op2, op1);

        } else {    // generic cast
            makeMeta(
                expr, '=ftoi', undefined,
                undefined,
                makeRValue(expr),
                '#1.0'
            );
        }
    };

    /* -----------------------------------------------------------
     *  UNARY_OPS dispatch table
     * -------------------------------------------------------- */

    UNARY_OPS = {};          /* will hold “=xxx” -> handler */

    /* no-op casts */
    function noop() {}

    /* register the handlers */
    map(UNARY_OPS,
        '=float',     noop,
        '=funcptr',   noop,
        '=int',       noop,
        '=pointer',   noop,

        '=*',         dereference,
        '=&',         reference,
        '=-',         minus,
        '=~',         not,
        '=abs',       absFloor,
        '=itof',      intToFloatConvert,
        '=ftoi',      floatToIntConvert,
        '=floor',     absFloor
    );

    /* -----------------------------------------------------------
     *  Generic unary operator
     * -------------------------------------------------------- */
    unaryOp = function (operator, expr, sourceCode, sourceOffset) {

        expr = metaSlot(expr);

        /* *structPointer -> a struct place (through-pointer field access / whole-struct assign) */
        if (operator === '*' && expr.type === 'p' && isStructAtom(expr.elem)) {
            setPlace(expr, 'pointer', makeRValue(expr), 0, expr.elem);
            return;
        }

        /* &structValue -> a typed struct pointer (materialize the place's address) */
        if (operator === '&' && expr.place) {
            var structName = expr.struct;
            var addr = placeAddress(expr);
            makeMeta(expr, ':=', 'p', undefined, addr, undefined);
            setElem(expr, structName);
            return;
        }

        var key      = '=' + operator;                // e.g. "=abs"
        var prevType = expr.type;                     /* for element-type propagation (Impala 2) */
        var prevElem = expr.elem;

        /* check type support */
        var sig  = key + expr.type;                   // e.g. "=absf"
        var rTyp = SUPPORTED_OPS[ sig ];
        if (rTyp == null) {
            typeError('Invalid type ({$type1})',
                               sourceCode, sourceOffset, expr.type, undefined, 'E302');
        }

        /* dispatch actual work */
        var fn = UNARY_OPS[ key ];
        if (fn) {
            fn(key, expr, sourceCode, sourceOffset);
        }

        /* update resulting type */
        expr.type = rTyp;

        /* element-type propagation (Impala 2) */
        if (key === '=*') {
            if (prevElem !== undefined) {
                expr.type = descHead(prevElem);          /* typed dereference: no cast needed */
                expr.elem = descTail(prevElem);
            } else {
                expr.elem = undefined;
            }
        } else if (key === '=&') {
            expr.elem = (prevType === undefined ? undefined       /* &x yields a pointer to x's type */
                    : prevType + (prevElem !== undefined ? ':' + prevElem : ''));
        } else {
            expr.elem = undefined;                                /* casts and numeric ops erase */
        }
    };

    /* -----------------------------------------------------------
     *  Symbol declaration helper
     * -------------------------------------------------------- */

    function declare(kind, scope, name, type, readonly, value, sourceCode, sourceOffset, comment, elem) {
        /* emit data / flush pending code */
        if (kind !== undefined) {
            flushMetaCode('');

            if (typeof output === 'function') {
                var line = '';
                if (scope === 'locals') line += (typeof TAB !== 'undefined' ? TAB : '\t');
                if (name != null)       line += name + ':';
                line += '\t' +
                        replace(kind, '?', TYPE_SUFFIXES[type] || '');
                if (value !== undefined) line += ' ' + value;
                if (comment)             line += '\t; ' + comment;

                output( line );
            }
        }

        /* give any temporary back to the pool */
        returnBack(value);

        /* register in symbol table */
        if (name !== undefined) {
            var table = symbols[scope];
            var prev  = table && table[name];

            if (prev) {
                if (kind !== undefined && prev.kind !== undefined) {
                    fail('Identifier already declared: ' + name,
                                  sourceCode, sourceOffset, 'E401');
                }
                if (type !== prev.type) {
                    typeError('Type mismatch with previous declaration of ' +
                                       name + ' (was {$type1})',
                                       sourceCode, sourceOffset, prev.type, undefined, 'E402');
                }
                if (prev.elem !== undefined && elem !== undefined && prev.elem !== elem) {
                    fail('Element type mismatch with previous declaration of ' + name
                                  + ' (was ' + elemVerbose(prev.elem) + ', now '
                                  + elemVerbose(elem) + ')',
                                  sourceCode, sourceOffset, 'E203');
                }
                /* inherit old flags */
                kind     = (kind     !== undefined ? kind     : prev.kind);
                readonly = (readonly || prev.readonly);
                elem     = (elem     !== undefined ? elem     : prev.elem);
            }

            /* store / update */
            if (!table) symbols[scope] = table = {};
            table[name] = {
                type: type,
                elem: elem,
                readonly: !!readonly,
                kind: kind,
                signature: prev && prev.signature,
                sourceCode: sourceCode,
                sourceOffset: sourceOffset,
                sourceName: (sourceName !== undefined ? sourceName
                                                               : (prev ? prev.sourceName : undefined))
            };
        }
    };

    /* -----------------------------------------------------------
     *  Flush all queued meta-code into final text output
     * -------------------------------------------------------- */
    flushMetaCode = function (prefix) {

        prefix = prefix || '';
        var TABstr = (typeof TAB !== 'undefined') ? TAB : '\t';

        var nextLabel   = TABstr;   // pending label prefix
        var nextComment = '';       // pending trailing comment
        function formatOperand(op) {
            return (op == null ? '' : op);
        }

        for (var i = 0; i < metacode.length; ++i) {

            var rec   = metacode[i];
            var op    = rec.operator;

            if (op == null) {
                /* empty / removed meta - skip */
                continue;
            }

            /* -------------------------------------------------- */
            /* handle a stand-alone label (“<--”)                 */
            /* -------------------------------------------------- */
            if (op === '<--') {
                assert(rec.operands[0][0] === '@',
                       "label must start with '@'");
                if (nextLabel !== TABstr) {
                    output(prefix + nextLabel + 'NOOP');
                }
                nextLabel = rec.operands[0].substr(1) + ':' + TABstr;
                continue;
            }

            /* -------------------------------------------------- */
            /* comment pseudo-op (“; ...”)                          */
            /* -------------------------------------------------- */
            if (op === ';') {
                nextComment = '\t; ' +
                             replace(rec.operands[0], '\t', ' ');
                continue;
            }

            /* -------------------------------------------------- */
            /* compile-time op (string starts with '<> ')         */
            /* -------------------------------------------------- */
            if (op.substr(0, 3) === '<> ') {

                if (nextLabel !== TABstr) {
                    output(prefix + nextLabel + 'NOOP');
                }
                nextLabel = TABstr;

                var gop = META_TO_GAZL[ op.substr(3) ];
                gop      = replace(gop, '?',
                                   TYPE_SUFFIXES[ rec.type ]);

                output(prefix + '\t! ' + gop + ' ' +
                       formatOperand(rec.operands[0]) + ' '   +
                       formatOperand(rec.operands[1]) + ' '   +
                       formatOperand(rec.operands[2]) + nextComment);

                nextComment = '';
                continue;
            }

            /* -------------------------------------------------- */
            /* normal run-time instruction                        */
            /* -------------------------------------------------- */
            var gInstr = META_TO_GAZL[ op ];
            gInstr     = replace(gInstr, '?',
                                 TYPE_SUFFIXES[ rec.type ]);

            output(prefix + nextLabel + gInstr + ' ' +
                   formatOperand(rec.operands[0]) + ' ' +
                   formatOperand(rec.operands[1]) + ' ' +
                   formatOperand(rec.operands[2]) + nextComment);

            nextLabel   = TABstr;
            nextComment = '';
        }

        /* reset queue */
        metacode.length = 0;
    };

    /* -----------------------------------------------------------
     *  Identifier lookup helper
     * -------------------------------------------------------- */
    lookup = function (x, name, isGlobal, sourceCode, sourceOffset) {

        var sym = symbols;
        var p   = null;

        /* local  ------------------------------------------------*/
        if (!isGlobal && (p = sym.locals['$' + name])) {

            if (p.type === 'S') {                                 /* struct value local -> a place */
                setPlace(x, 'local', '$' + name, 0, p.elem);
                return;
            }
            if (p.type === 'A') {
                makeMeta(x, '=&', 'p', undefined,
                                  '$' + name, '*0');
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=' : '='),
                                  p.type,
                                  undefined,
                                  '$' + name,
                                  undefined);
            }
            setElem(x, p.elem);
            return;
        }

        /* global -----------------------------------------------*/
        if (isGlobal && (p = sym.globals[name])) {

            if (p.type === 'S') {                                 /* struct value global -> a place in global memory */
                setPlace(x, 'globalAddr', '&' + name, 0, p.elem);
                return;
            }
            if (p.type === 'A') {
                makeMeta(x, ':=', 'p', undefined,
                                  '&' + name, undefined);
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=*' : '=*'),
                                  p.type,
                                  undefined,
                                  '&' + name,
                                  undefined);
            }
            setElem(x, p.elem);
            return;
        }

        /* function ---------------------------------------------*/
        if ((p = sym.functions[name])) {
            if (p.type === 'N') {
                makeMeta(x, ':=', 'N', undefined,
                                  '^' + name, undefined);
            } else {
                assert(p.type === 'U', 'function entry must be U');
                makeMeta(x, ':=', 'F', undefined,
                                  '&' + name, undefined);
            }
            setElem(x, undefined);
            return;
        }

        /* constant / #define -----------------------------------*/
        if ((p = sym.defines[name])) {
            makeMeta(x, ':=', p.type, undefined,
                              '#' + name, undefined);
            setElem(x, p.elem);
            return;
        }

        /* not found --------------------------------------------*/
        fail('Undeclared identifier: ' + name,
                      sourceCode, sourceOffset, 'E403');
    };

    /* -----------------------------------------------------------
     *  Ensure expression resolves to a compile-time constant
     * -------------------------------------------------------- */
    makeConstant = function (x, wantType,
                                      sourceCode, sourceOffset) {

        var r = makeRValue(x, '#<&');

        if (x.type !== wantType ||
            span(r[0], '#<&') !== 1) {

            fail(
                bake('Expected constant ' +
                     VERBOSE_TYPES[ wantType ]),
                sourceCode, sourceOffset, 'E407');
        }
        return r;
    };

    /* -----------------------------------------------------------
     *  Constant subtraction helper
     * -------------------------------------------------------- */
    subConstInt = function (opL, opR) {

        assert(span(opR[0], '#<') === 1,
               "rhs must be const");

        /* trivial cases */
        if (opR === '#0') return opL;

        if (opL[0] === '#' && opR[0] === '#' &&
            span(opL.substr(1), '0123456789') === opL.length - 1 &&
            span(opR.substr(1), '0123456789') === opR.length - 1) {

            return '#' + ( parseInt(opL.substr(1), 10) -
                           parseInt(opR.substr(1), 10) );
        }

        /* need run-time temp */
        returnBack(opL);

        var tmp;
        if (span(opL[0], '#<') === 1) {
            emit('<> -', 'i',
                          tmp = borrow('<'),
                          opL, opR);
        } else {
            emit('-', 'i',
                          tmp = borrow('%'),
                          opL, opR);
        }
        return tmp;
    };

    /* drop leading “#” helper */
    dropHash = function (s) {
        return (s[0] === '#') ? s.substr(1) : s;
    };

    /* printable ASCII table (33-126) */
    printable = '';
    for (var i = 33; i < 127; ++i) {
        printable += char(i);
    }

    /* -----------------------------------------------------------
     *  Dump a string constant into assembly
     * -------------------------------------------------------- */
    dumpString = function (label, str) {

        var len = str.length;
        declare('CNST', 'globals', label, '?',
                         true, '*' + len, '', 0);

        var offset = 0;
        while (offset < len) {

            /* raw bytes before printable chunk */
            var non = find(str.substr(offset), printable);
            if (non > 0) {
                var d = '';
                for (var k = 0; k < non; ++k) {
                    d += ' #' + ordinal(str[offset + k]);
                }
                declare('DATi', undefined, undefined, 'i',
                                 true, d.substr(1), '', 0);
                offset += non;
                continue;
            }

            /* printable (plus spaces) */
            var spanLen = span(str.substr(offset),
                               printable + ' ');
            spanLen = rspan(str.substr(offset, spanLen), ' ');
            if (spanLen > 0) {
                declare('DATs', undefined, undefined, 'i',
                                 true, str.substr(offset, spanLen), '', 0);
                offset += spanLen;
            }
        }
    };

    /* -----------------------------------------------------------
     *  Manage / share string literals
     * -------------------------------------------------------- */
    makeString = function (prefix, x, s,
                                    sourceCode, sourceOffset) {

        s += char(0);       // NUL-terminate

        var byteString = '';
        for (var idx = 0; idx < s.length; ++idx) {
            byteString += char(ordinal(s[idx]));
        }
        s = byteString;

        var tbl = strings[prefix];
        if (!tbl) {
            tbl = strings[prefix] = [];
            tbl.rlookup = {};
        }

        var entry = tbl.rlookup[s];
        if (entry == null) {

            /* generate unique label */
            var name = '.' + prefix + '_' +
                       (s.replace(/[^0-9a-zA-Z]/g, '')
                          .substr(0, 6)) +
                       '_' +
                       ( ((randomId + tbl.length) >>> 0)
                         .toString(16) );

            if (noForward) {
                dumpString(name, s);
            } else {
                tbl.push({ name:name, data:s });
            }

            /* add all suffixes to rlookup */
            for (var k = 0; k < s.length; ++k) {
                tbl.rlookup[ s.substr(k) ] =
                    name + (k ? ':' + k : '');
            }
            entry = name;            // full string label
        }

        makeMeta(x, ':=', 'p',
                          undefined, '&' + entry, undefined);
    };

    /* -----------------------------------------------------------
     *  Tiny utilities still missing from the toolbox
     * -------------------------------------------------------- */

    /* wipe an Array or plain Object in-place */
    function prune(o) {
        if (!o) return;
        if (o instanceof Array) {
            o.length = 0;
        } else {
            for (var k in o) {
                delete o[k];
            }
        }
    }

    /* simple “foreach” - fn(element, index) */
    function iterate(arr, fn) {
        for (var i = 0; i < arr.length; ++i) {
            fn(arr[i], i);
        }
    }

    /* -----------------------------------------------------------
     *  Compiler start / end hooks
     * -------------------------------------------------------- */

    start = function () {

        /* reset per-compilation state */
        if (!stock) stock = { '%': [], '<': [] };
        if (!counters) counters = { '%': 0, '<': 0 };
        if (!metacode) metacode = [];
        if (!symbols) symbols = {};
        if (!strings) strings = { s: [], a: [] };
        if (!switchStack) switchStack = [];

        /* clear transient pools and counters */
        var poolPercent = stock['%'] || (stock['%'] = []);
        var poolAngle   = stock['<'] || (stock['<'] = []);
        resetQueue(poolPercent);
        resetQueue(poolAngle);
        counters['%'] = 0;
        counters['<'] = 0;

        /* wipe accumulated meta-instructions */
        metacode.length = 0;
        labelCounter    = 0;
        switchStack.length = 0;

        /* reset symbol tables */
        symbols.locals   = {};
        symbols.globals  = {};
        symbols.functions = {};
        symbols.defines  = {};
        structs          = {};
        functypes        = {};
        exportNext       = false;

        /* reset deferred string tables */
        strings.s = [];
        strings.s.rlookup = {};
        strings.a = [];
        strings.a.rlookup = {};

        noForward = false;

        /* random-id seeding */
        if (typeof hostRandomId !== 'undefined') {
            randomId = hostRandomId;
        } else {
            for (var i = 0; i < 1000; ++i) {
                randomId =
                    floor(random() * 0xFFFFFFFF) ^ time();
            }
        }

        /* banner */
        var LF = '\n';
        output('; Compiled with Impala version ' +
               IMPALA_VERSION + LF);
        output('; signatures version=1');
    };

    end = function () {

        /* dump deferred string literals */
        iterate(strings.s, function (rec) {
            dumpString(rec.name, rec.data);
        });

        /* dump assert strings only if present */
        if (strings.a.length > 0) {

            output('\t! EQUi #DEBUG #0 @.noAssertStrings');

            iterate(strings.a, function (rec) {
                dumpString(rec.name, rec.data);
            });

            output('.noAssertStrings:\t!');
        }
    };
};function root($){return (function(){var _b=_i;return _($)&&(function(){ start(); ; return true})()&&((function(){while((function(){var _b=_i;return ImportDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExportDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||StructDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FuncTypeDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExternDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _l=_i,_x=(!!_s[_i])&&(++_i,true);_i=_l;return !_x})()&&(function(){ end(); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ImportDecl($){var $path=createParserContext();return (function(){var _b=_i;return IMPORT($)&&_($)&&StringLiteral($path)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExportDecl($){return (function(){var _b=_i;return EXPORT($)&&_($)&&(function(){ exportNext = true; ; return true})()&&(function(){var _b=_i;return FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ exportNext = false; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StructDecl($){var $id=createParserContext(),$sname,$f=createParserContext();return (function(){var _b=_i;return STRUCT($)&&_($)&&Identifier($id)&&(function(){ $sname = $id._; beginStruct($id._, _s, _i); ; return true})()&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return ArrayDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ addStructField($sname, { name: $f.name, type: $f.type, elem: $f.elem, struct: $f.struct, size: $f.size }, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ endStruct($sname); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncTypeDecl($){var $id=createParserContext(),$ftname,$p=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return FUNCTYPE($)&&_($)&&Identifier($id)&&(function(){ $ftname = $id._; beginFuncType($id._, _s, _i); ; return true})()&&(_s[_i]==="(")&&(++_i,true)&&_($)&&((function(){var _b=_i;return TypeDeclr($p)&&(function(){ addFuncTypeParam($ftname, $p.type, $p.elem, $p.struct, $p.words, $p.name); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($p)&&(function(){ addFuncTypeParam($ftname, $p.type, $p.elem, $p.struct, $p.words, $p.name); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&((function(){var _b=_i;return RETURNS($)&&_($)&&TypeDeclr($r)&&(function(){ addFuncTypeReturn($ftname, $r.type, $r.elem, $r.struct, $r.words, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($r)&&(function(){ addFuncTypeReturn($ftname, $r.type, $r.elem, $r.struct, $r.words, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ endFuncType($ftname); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TypeDeclr($){var $base=createParserContext(),$desc,$id=createParserContext();return (function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&((function(){var _b=_i;return Identifier($id)&&(function(){ $.name = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ var head = descHead($desc); var tail = descTail($desc); if (tail === undefined && isStructAtom(head)) { $.type = 'S'; $.struct = head; $.elem = undefined; $.words = structWords(head); } else if (tail === undefined && isFuncTypeAtom(head)) { $.type = 'F'; $.elem = head; $.struct = undefined; $.words = 1; } else { $.type = head; $.elem = tail; $.struct = undefined; $.words = undefined; } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncDecl($){var $id=createParserContext(),$inp=createParserContext(),$out=createParserContext(),$v=createParserContext(),$,$loc=createParserContext();return (function(){var _b=_i;return FUNCTION($)&&_($)&&Identifier($id)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ assert(validateStock('%')); assert(validateStock('<')); output(''); output(';-----------------------------------------------------------------------------'); /* declare the function symbol */ declare( undefined, 'functions', $id._, 'U', true, undefined, _s, _i ); var entry = symbols.functions[$id._]; if (entry) { if (!entry.signature) { entry.signature = {}; } entry.signature.params = []; entry.signature.returns = '?'; entry.signature.returnElem = undefined; entry.signature.returnName = undefined; entry.signature.sourceCode = _s; entry.signature.sourceOffset = _i; entry.signature.sourceName = sourceName; entry.signature.returnResolved = false; entry.pendingReturnPlaceholder = undefined; entry.pendingReturnDeclaration = undefined; } ; return true})()&&ArgsDecl($inp)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){var _b=_i;return RETURNS($)&&_($)&&VarDecl($out)&&(function(){ var entry = symbols.functions[$id._]; if (entry) { entry.pendingReturns = undefined; addReturn(entry, $out.name, $out.type, $out.elem, $out.size, _s, _i, $out.struct, $out.words); } ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){ var entry = symbols.functions[$id._]; if (entry) addReturn(entry, $v.name, $v.type, $v.elem, $v.size, _s, _i, $v.struct, $v.words); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ var entry = symbols.functions[$id._]; if (entry && entry.signature) { var rl = entry.pendingReturns; entry.signature.returnList = rl; entry.signature.returnCount = rl.length; entry.signature.returns = rl[0].type; entry.signature.returnElem = rl[0].elem; entry.signature.returnName = rl[0].rawName; entry.signature.returnStruct = rl[0].struct; var _rw = 0;                    /* total output-window words (struct returns span >1) */ for (var _wi = 0; _wi < rl.length; ++_wi) _rw += (rl[_wi].type === 'S' ? rl[_wi].words : 1); entry.signature.returnWords = _rw; resolveFunctionReturnType($id._, rl[0].type, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ /* implicit 1-word return: even void functions expose a single-word PARA so legacy call sites keep a deterministic return slot and the JSPEG output matches the historical PPEG layout. */ var entry = symbols.functions[$id._]; if (entry) { entry.pendingReturns = undefined; entry.pendingReturnPlaceholder = { sourceCode: _s, sourceOffset: _i }; } if (entry && entry.signature) { entry.signature.returns = '?'; entry.signature.returnElem = undefined; entry.signature.returnName = undefined; entry.signature.returnCount = 0; entry.signature.returnList = []; resolveFunctionReturnType($id._, '?', _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ /* declare input parameters */ var entry = symbols.functions[$id._]; if (entry && entry.signature) { entry.signature.params = []; for (var idx = 0; idx < $inp.n; ++idx) { var param = $inp._[idx]; entry.signature.params.push({ type: param.type, elem: param.elem, name: param.name, size: param.size, struct: param.struct, words: param.words }); } } emitFunctionSignature($id._); if (entry) { if (entry.pendingReturns && entry.pendingReturns.length > 0) { for (var _ri = 0; _ri < entry.pendingReturns.length; ++_ri) { var ret = entry.pendingReturns[_ri]; if (ret.type === 'S') {  /* by-value struct return -> leading output PARA *sizeof (a writable place) */ declare( 'PARA', 'locals', ret.name, 'S', false, '*' + ret.words, ret.sourceCode, ret.sourceOffset, undefined, ret.struct ); } else { declare( 'OUT?', 'locals', ret.name, ret.type, false, ret.size, ret.sourceCode, ret.sourceOffset, undefined, ret.elem ); } } entry.pendingReturns = undefined; } else if (entry.pendingReturnPlaceholder) { var placeholder = entry.pendingReturnPlaceholder; declare( 'PARA', 'locals', undefined, '?', false, '*1', placeholder.sourceCode, placeholder.sourceOffset ); entry.pendingReturnPlaceholder = undefined; } } iterate($inp._, function (p) { if (p.type === 'S') {         /* by-value struct param -> labeled input PARA *sizeof (read-only place) */ declare( 'PARA', 'locals', '$' + p.name, 'S', true, '*' + p.words, _s, _i, undefined, p.struct ); } else { declare( 'INP?', 'locals', '$' + p.name, p.type, true, (p.size !== undefined ? '*' + p.size : undefined), _s, _i, undefined, p.elem ); } }); ; return true})()&&((function(){var _b=_i;return LOCALS($)&&_($)&&LocalsDecl($loc)&&(function(){ iterate($loc._, function (v) { if (v.type === 'S') {         /* struct value local -> LOCA *sizeof, remember struct */ declare( 'LOCA', 'locals', '$' + v.name, 'S', false, '*' + v.words, _s, _i, undefined, v.struct ); } else { declare( 'LOC?', 'locals', '$' + v.name, v.type, false, (v.words !== undefined ? '*' + v.words : undefined), _s, _i, undefined, v.elem ); } }); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ output(';-----------------------------------------------------------------------------'); ; return true})()&&Block($)&&(function(){ /* wrap-up body */ processBranches(); emit('--^', undefined, undefined, undefined, undefined); flushMetaCode('\t'); prune(symbols.locals); labelCounter = 0; output(''); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExternDecl($){var $id=createParserContext(),$desc,$type=createParserContext();return (function(){var _b=_i;return EXTERN($)&&_($)&&(function(){ $.scope = 'globals'; $.structFwd = false; ; return true})()&&(function(){var _b=_i;return STRUCT($)&&_($)&&Identifier($id)&&(function(){ $.structFwd = true; externStruct($id._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){var _b=_i;return FUNCTION($)&&(function(){ $.type  = 'U';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NATIVE($)&&(function(){ $.type  = 'N';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&Identifier($id)&&(function(){ $.name  = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ $desc = undefined; ; return true})()&&((function(){var _b=_i;return BASE_TYPE($type)&&_($)&&(function(){ $desc = CASTS_TO_TYPES[$type._]; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&ARRAY($)&&_($)&&Identifier($id)&&(function(){ $.type = 'A'; $.name = $id._; $.elem = $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ if ($.structFwd) return true; declare( undefined,                 // no section for extern
                                                                             $.scope, $.name, $.type, false,                     // not readonly
                                                                             '?', _s, _i, undefined, $.elem ); if ($.scope === 'functions') { var entry = symbols.functions[$.name]; var signature = entry && entry.signature; if (entry) { if (!signature) { signature = entry.signature = {}; } if (signature.sourceName === undefined) { signature.sourceName = sourceName; } if (signature.sourceCode === undefined) { signature.sourceCode = _s; signature.sourceOffset = _i; signature.sourceName = sourceName; } signature.returnResolved = false; } var role = ($.type === 'N' ? 'extern native' : 'extern func'); var placeholderSignature = { params: [], returns: undefined, sourceName: sourceName, sourceCode: _s, sourceOffset: _i, }; emitStandaloneSignatureComment( formatFunctionSignatureComment( $.name, placeholderSignature, role, sourceName, _s, _i ) ); } else if ($.scope === 'globals') { emitStandaloneSignatureComment( formatGlobalSignatureComment( 'GLOB', $.name, $.type, $.size, 'extern', sourceName, _s, _i, $.elem ) ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ConstDecl($){var $type=createParserContext(),$desc,$nf,$t,$telem,$id=createParserContext(),$x=createParserContext();return (function(){var _b=_i;return CONST($)&&_($)&&BASE_TYPE($type)&&_($)&&(function(){ $desc = CASTS_TO_TYPES[$type._]; $nf = noForward; noForward = true; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ $t     = descHead($desc); $telem = descTail($desc); ; return true})()&&Identifier($id)&&(function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($x)&&(function(){ declare( '! DEF?', 'defines', $id._, $t, true, makeConstant($x._, $t, _s, _i), _s, _i, formatConstSignatureComment( $id._, $t, sourceName, _s, _i, $telem ), $telem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ declare( undefined, 'defines', $id._, $t, true, undefined, _s, _i, undefined, $telem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ noForward = $nf; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GlobalDecl($){var $section,$v=createParserContext(),$vStruct,$init,$d=createParserContext(),$binit,$x=createParserContext(),$a=createParserContext(),$aStructEl,$aStruct,$aCount;return (function(){var _b=_i;return (function(){var _b=_i;return GLOBAL($)&&(function(){ $section = 'GLOB'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||READONLY($)&&(function(){ $section = 'CNST'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||TEMPORARY($)&&(function(){ $section = 'TEMP'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&(function(){var _b=_i;return VarDecl($v)&&(function(){ $vStruct = ($v.type === 'S'); if ($vStruct) {              /* struct value global -> one zeroed GLOB/CNST/TEMP *sizeof */ declare( $section, 'globals', $v.name, 'S', ($section === 'CNST'), '*' + $v.words, _s, _i, formatGlobalSignatureComment( $section, $v.name, 'S', undefined, undefined, sourceName, _s, _i, $v.struct), $v.struct ); } else { declare( $section, 'globals', undefined, $v.type, ($section === 'CNST'), '*1', _s, _i ); $init = ZEROES[$v.type]; } ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){var _b=_i;return Braced($d)&&(function(){ if (!$vStruct) fail('Brace initializers are only for struct values', _s, _i, 'E422'); $binit = []; buildStructInit($v.struct, $d._, $binit, _s, _i); emitInitData($binit, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($x)&&(function(){ if ($vStruct) fail('A struct value needs a brace initializer', _s, _i, 'E421'); $init = makeConstant($x._, $v.type, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ if (!$vStruct) declare( 'DAT?', 'globals', $v.name, $v.type, ($section === 'CNST'), $init, _s, _i, formatGlobalSignatureComment( $section, $v.name, $v.type, undefined, undefined, sourceName, _s, _i, $v.elem ), $v.elem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($a)&&(function(){ declare( $section, 'globals', $a.name, 'A', ($section === 'CNST'), '*' + $a.words, _s, _i, formatGlobalSignatureComment( $section, $a.name, 'A', $a.size, undefined, sourceName, _s, _i, $a.elem ), $a.elem ); $aStructEl = ($a.elem !== undefined && descTail($a.elem) === undefined && isStructAtom(descHead($a.elem))); $aStruct = $a.elem; $aCount = parseInt('' + $a.size, 10); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){var _b=_i;return InitList($d)&&(function(){   /* flat list -> scalar-element arrays only */ if ($aStructEl) fail('A struct-element array needs nested braces, one group per element', _s, _i, 'E422'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Braced($d)&&(function(){   /* nested braces -> struct-element arrays */ if (!$aStructEl) fail('Nested brace initializers are for struct-element arrays', _s, _i, 'E422'); var _arr = $d._; $binit = []; for (var _ae = 0; _ae < $aCount; ++_ae) { var _aev = (_ae < _arr.length) ? _arr[_ae] : undefined; buildStructInit($aStruct, (_aev && _aev.braced) || [], $binit, _s, _i); } emitInitData($binit, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Braced($){var $i=createParserContext();return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return BracedItem($i)&&(function(){ $._[$.n++] = $i._; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&BracedItem($i)&&(function(){ $._[$.n++] = $i._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BracedItem($){var $b=createParserContext(),$x=createParserContext();return (function(){var _b=_i;return Braced($b)&&(function(){ $._ = { braced: $b._ }; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($x)&&(function(){ var m = metaSlot($x._); var op = makeRValue(m, '#<&'); if (span(op[0] || '', '#<&') !== 1) fail('Initializer must be a constant', _s, _i, 'E407'); $._ = { op: op, type: m.type }; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function InitList($){var $d,$type,$x=createParserContext();return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $d = ' '; $type = undefined; ; return true})()&&((function(){var _b=_i;return Expr($x)&&(function(){ var xMeta = metaSlot($x._); $type = xMeta.type; $d += makeConstant(xMeta, $type, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Expr($x)&&(function(){ var xMeta = metaSlot($x._); var xType = xMeta.type; var constant = makeConstant(xMeta, xType, _s, _i); /* decide if we need to flush DATA */ if (  constant[0] === '<' || $d[1] === '<' || ($d + ' ' + constant).length >= 55) { declare( 'DATA', 'globals', undefined, xType, true, $d.substr(1), _s, _i ); $d = ''; } $d += ' ' + constant; $type = xType; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ if ($d.substr(1) !== '') { declare( 'DATA', 'globals', undefined, $type, true, $d.substr(1), _s, _i ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArgsDecl($){var $v=createParserContext();return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return VarDecl($v)&&(function(){ var entry = {}; entry.type = $v.type; entry.elem = $v.elem; entry.struct = $v.struct; entry.words = $v.words; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){ var entry = {}; entry.type = $v.type; entry.elem = $v.elem; entry.struct = $v.struct; entry.words = $v.words; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LocalsDecl($){var $v=createParserContext();return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return (function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ var entry = {}; entry.type = $v.type; entry.elem = $v.elem; entry.struct = $v.struct; entry.words = $v.words; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ var entry = {}; entry.type = $v.type; entry.elem = $v.elem; entry.struct = $v.struct; entry.words = $v.words; entry.name = $v.name; entry.size = $v.size; $._[$.n++] = entry; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TypeBase($){var $t=createParserContext(),$id=createParserContext();return (function(){var _b=_i;return BASE_TYPE($t)&&_($)&&(function(){ $._ = CASTS_TO_TYPES[$t._]; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Identifier($id)&&(function(){ if (!isStructAtom($id._) && !isFuncTypeAtom($id._)) fail('Unknown type ' + $id._, _s, _i, 'E413'); $._ = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function VarDecl($){var $base=createParserContext(),$desc,$id=createParserContext();return (function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&Identifier($id)&&(function(){ var head = descHead($desc); var tail = descTail($desc); if (tail === undefined && isStructAtom(head)) { $.type = 'S'; $.struct = head; $.elem = undefined; $.words = structWords(head); } else if (tail === undefined && isFuncTypeAtom(head)) { $.type = 'F';           /* named funcptr type -> a funcptr carrying its type tag */ $.elem = head; $.struct = undefined; $.words = undefined;    /* scalar funcptr: a single word, no size operand */ } else { $.type = head; $.elem = tail; $.struct = undefined; $.words = undefined; } $.name = $id._; $.size = undefined; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArrayDecl($){var $desc,$base=createParserContext(),$id=createParserContext(),$x=createParserContext(),$size,$;return (function(){var _b=_i;return (function(){ $desc = undefined; ; return true})()&&((function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = 'p:' + $desc; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&ARRAY($)&&_($)&&Identifier($id)&&(_s[_i]==="[")&&(++_i,true)&&_($)&&Expr($x)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ $size = makeConstant($x._, 'i', _s, _i); $.type = 'A'; $.elem = $desc; $.name = $id._; $.size = dropHash($size); /* allocation words = count * element size (structs are multi-word) */ if ($desc !== undefined && descTail($desc) === undefined && isStructAtom(descHead($desc))) { if (!/^[0-9]+$/.test('' + $.size)) fail('A struct array size must be a numeric literal for now', _s, _i, 'E414'); $.words = parseInt($.size, 10) * structWords(descHead($desc)); } else { $.words = $.size;   /* scalar element: 1 word each */ } returnBack($size); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Statement($){var $label=createParserContext();return (function(){var _b=_i;return (function(){ var snippet = _s.substr(_i); var cut     = find(snippet, "{;\r\n"); var txt     = (cut >= 0 ? snippet.substr(0, cut) : snippet); emitMeta({ operator:';', type:undefined, operands:[ txt, undefined, undefined ] }); ; return true})()&&((function(){while((function(){var _b=_i;return Identifier($label)&&(_s[_i]===":")&&(++_i,true)&&_($)&&(function(){ emitMeta({ operator:'<--', type:undefined, operands:[ '@' + $label._, undefined, undefined ] }); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Assert($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Block($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Copy($)||(_im=(_i>_im?_i:_im),_i=_b,false)||DoWhile($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Loop($)||(_im=(_i>_im?_i:_im),_i=_b,false)||For($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Goto($)||(_im=(_i>_im?_i:_im),_i=_b,false)||If($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Switch($)||(_im=(_i>_im?_i:_im),_i=_b,false)||While($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Destructure($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ if (metaSlot($._).winBase !== undefined) {     /* discarded struct-return value: free its window + base */ returnBack(metaSlot($._).base); freeStructWindow($._); } releaseMeta($._); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Expr($){var $r=createParserContext();return (function(){var _b=_i;return Bitwise($)&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($r)&&(function(){ if (!dry) assign($._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Bitwise($){var $first,$op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return (function(){ $first = undefined; ; return true})()&&AddSub($)&&((function(){while((function(){var _b=_i;return BITWISE_OP($op)&&_($)&&AddSub($r)&&(function(){ if (!dry) { if ($first === undefined) $first = $op._; else if ($first !== $op._) mixedBitwise($first, $op._, _s, _i); binaryOp($op._, $._, $r._, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if (!dry) stampBitwise($._, $first !== undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function AddSub($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return MulDiv($)&&((function(){while((function(){var _b=_i;return ADDSUB_OP($op)&&_($)&&MulDiv($r)&&(function(){ if (!dry) binaryOp($op._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MulDiv($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return PrePost($)&&((function(){while((function(){var _b=_i;return MULDIV_OP($op)&&_($)&&PrePost($r)&&(function(){ if (!dry) mulDivOp($op._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PrePost($){var $op=createParserContext(),$cdesc,$cmods,$sid=createParserContext(),$pdepth;return (function(){var _b=_i;return (function(){var _b=_i;return PREFIX_OP($op)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&BASE_TYPE($op)&&_($)&&(function(){ $cdesc = CASTS_TO_TYPES[$op._]; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $cdesc = 'p:' + $cdesc; $cmods = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&Identifier($sid)&&(function(){ $pdepth = 0; ; return true})()&&((function(){for(var _n=0;(function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $pdepth = $pdepth + 1; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})();++_n);return _n>0})())&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if (!isStructAtom($sid._)) fail('Unknown type ' + $sid._, _s, _i, 'E413'); $cdesc = $sid._; for (var _pk = 0; _pk < $pdepth; ++_pk) $cdesc = 'p:' + $cdesc; $cmods = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&PrePost($)&&(function(){ if (!dry) { if ($cmods) { unaryOp('pointer', $._, _s, _i); setElem($._, descTail($cdesc)); } else { unaryOp($op._, $._, _s, _i); } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Value($)&&((function(){while((function(){var _b=_i;return FuncCall($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Subscript($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FieldAccess($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Subscript($){var $s=createParserContext();return (function(){var _b=_i;return (_s[_i]==="[")&&(++_i,true)&&_($)&&Expr($s)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ if (!dry) { var sb = metaSlot($._); if (sb.type === 'p' && isStructAtom(sb.elem)) structSubscript($._, $s._, _s, _i); else binaryOp('=[]', $._, $s._, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FieldAccess($){var $f=createParserContext();return (function(){var _b=_i;return (_s.substr(_i,2)==="->")&&(_i+=2,true)&&_($)&&Identifier($f)&&(function(){ if (!dry) fieldAccess($._, $f._, true, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===".")&&(++_i,true)&&_($)&&Identifier($f)&&(function(){ if (!dry) fieldAccess($._, $f._, false, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncCall($){var $type,$;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ if (!dry) { $.count = 0; /* how many leading output slots the callee expects (>1 = multi-return) */ var _c = metaSlot($._); var _rs = 1; if (_c.operator === ':=' && _c.operands[1] && (_c.operands[1][0] === '&' || _c.operands[1][0] === '^')) { var _e = symbols.functions[_c.operands[1].substr(1)]; if (_e && _e.signature && _e.signature.returnWords !== undefined && _e.signature.returnWords > 1) _rs = _e.signature.returnWords;   /* multi-scalar OR by-value struct return window */ } else if (_c.type === 'F' && isFuncTypeAtom(_c.elem)) { var _ft = functypes[_c.elem];   /* indirect call through a named funcptr type */ if (_ft.returnWords > 1) _rs = _ft.returnWords; } $.retSlots = _rs; $.words = 0;                            /* input words placed so far (struct args span >1) */ $.base  = borrowForCall(); for (var _os = 1; _os < _rs; ++_os)      /* reserve the extra output slots */ claimSlot($.base + _os); $.types = []; $.elems = []; $.nulls = []; } ; return true})()&&((function(){var _b=_i;return Argument($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Argument($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if (!dry) { var callee = metaSlot($._); var callResultType = '?'; var signature = null; var calleeName = null; if (span(callee.type, 'FN') !== 1) { typeError( 'Invalid type for function call ({$type1})', _s, _i, callee.type , undefined, 'E408'); } if (callee.operator === ':=' && callee.operands[1] && (callee.operands[1][0] === '&' || callee.operands[1][0] === '^')) { calleeName = callee.operands[1].substr(1); var entry = symbols.functions[calleeName]; if (entry && entry.kind === 'FUNC' && entry.signature) { signature = entry.signature; } } else if (callee.type === 'F' && isFuncTypeAtom(callee.elem)) { signature = functypes[callee.elem];   /* indirect call: check against the funcptr type */ } if (signature && signature.returnCount > 1 && !destructuring) { fail('Function ' + (calleeName || 'this call') + ' returns ' + signature.returnCount + ' values; destructure the call (a, b = ' + (calleeName || 'f') + '(...))', _s, _i, 'E431'); } if (signature) { var params = signature.params || []; var actualCount = ($.types ? $.types.length : 0); var expectedCount = params.length; var label = (calleeName || 'function'); if (actualCount !== expectedCount) { fail( 'Invalid argument count when calling ' + label + ' (expected ' + expectedCount + ', got ' + actualCount + ')', _s, _i , 'E405'); } for (var argIdx = 0; argIdx < expectedCount; ++argIdx) { var expected = params[argIdx].type; var actual = $.types[argIdx]; if (actual === undefined) { actual = '?'; } if (actual === '?' || expected === undefined) { continue; } if (actual !== expected) { typeError( 'Argument type mismatch when calling ' + label + ' ({$type1} vs expected {$type2})', _s, _i, actual, expected , 'E406'); } if (expected === 'S' && params[argIdx].struct !== undefined && $.elems[argIdx] !== params[argIdx].struct) { fail('Struct type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (expected ' + params[argIdx].struct + ', got ' + ($.elems[argIdx] || 'a non-struct value') + ')', _s, _i, 'E421'); } var expectedElem = params[argIdx].elem;   /* typed pointer param: assume loudly */ if (expected === 'p' && expectedElem !== undefined && !$.nulls[argIdx] && $.elems[argIdx] !== expectedElem) { fail('Pointer element type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (expected ' + elemVerbose(expectedElem) + ' elements, got ' + elemVerbose($.elems[argIdx]) + ' elements)', _s, _i, 'E202', 'use a cast: (' + elemVerbose(expectedElem) + ' pointer)'); } } if (signature.returnResolved && signature.returns !== undefined) { callResultType = signature.returns; } else if (signature.expectedReturn !== undefined) { callResultType = signature.expectedReturn; } else if (signature.returns !== undefined) { callResultType = signature.returns; } } var callComment = formatCallExpectationComment( calleeName, signature, $.types, callResultType, sourceName, _s, _i ); var commentIndex = -1; if (callComment) { commentIndex = metacode.length; emit(';', undefined, callComment, undefined, undefined); commentIndex = metacode.length - 1; } var func = makeRValue(callee, '&^$%'); emit('()', '?', func, '%' + $.base, '*' + ($.words + $.retSlots)); returnBack(func); while ($.words-- > 0) {              /* free the argument words (past the output slots) */ returnBack('%' + ($.base + $.retSlots + $.words)); } makeMeta(callee, ':=', callResultType, undefined, '%' + $.base, undefined); setElem(callee, undefined); if (signature && signature.returns === 'S') {   /* by-value struct return -> a place over the output window */ var _wp = borrow('%'); emit('=&', 'p', _wp, '%' + $.base, '*' + $.retSlots); setPlace(callee, 'pointer', _wp, 0, signature.returnStruct); callee.winBase  = $.base;       /* output window slots to free once the value is consumed */ callee.winWords = $.retSlots; } else if ($.retSlots > 1) {        /* multi-return: expose window for destructuring */ callee.multiBase = $.base; callee.multiCount = $.retSlots; callee.multiReturnList = signature.returnList; } if (calleeName) { callee.callInfo = { name: calleeName, commentIndex: commentIndex, commentArgs: { name: calleeName, signature: signature, actualTypes: ($.types ? $.types.slice() : undefined), sourceName: sourceName, sourceCode: _s, sourceOffset: _i } }; } else if (callee.callInfo) { callee.callInfo = undefined; } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Argument($){var $a=createParserContext();return (function(){var _b=_i;return Expr($a)&&(function(){ if (!dry) { ++$.count; var meta = metaSlot($a._); if ($.types) { $.types.push(meta.type); } if ($.elems) {                       /* element chain + null-ness, captured */ $.elems.push(meta.elem);         /* before makeArgValue mutates the meta */ $.nulls.push(meta.type === 'p' && meta.operands[1] === '&NULL' && meta.operands[2] === undefined); } var winSlot = $.base + $.retSlots + $.words; if (meta.type === 'S') {              /* by-value struct argument spans sizeof words */ var w = structWords(meta.struct); copyStructArg($a._, winSlot, w); $.words += w; } else { makeArgValue($a._, winSlot); $.words += 1; } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Group($){return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if (!dry) stampBitwise($._, false); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BoolGroup($){var $label;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ $label = undefined; ; return true})()&&And($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="||")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('t'); } emit('?->', true, $label, undefined, undefined); ; return true})()&&And($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if ($label !== undefined) { emit('<-?', true, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function And($){var $label;return (function(){var _b=_i;return (function(){ $label = undefined; ; return true})()&&Comp($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="&&")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('f'); } emit('?->', false, $label, undefined, undefined); ; return true})()&&Comp($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if ($label !== undefined) { emit('<-?', false, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Comp($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return (_s[_i]==="!")&&(++_i,true)&&_($)&&Comp($)&&(function(){ emit('!', undefined, undefined, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = true; ; return true})()&&(function(){var _l=_i,_x=Group($);_i=_l;return !_x})()&&(function(){ dry = false; ; return true})()&&BoolGroup($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = false; ; return true})()&&Expr($)&&COMP_OP($op)&&_($)&&Expr($r)&&(function(){ checkCompMix($._, $r._, _s, _i); binaryOp($op._, $._, $r._, _s, _i); emitMeta($._); releaseMeta($._); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Assert($){var $okLabel,$start,$exprText,$str=createParserContext(),$;return (function(){var _b=_i;return ASSERT($)&&_($)&&(function(){ $okLabel = newLabel('a'); emit('<> ==', 'i', '#DEBUG', '#0', $okLabel); $start    = _i; $exprText = ''; ; return true})()&&BoolGroup($str)&&(function(){ $exprText = _s.substring($start, _i); $exprText = $exprText.replace(/[ \t\r\n]+$/, ''); ; return true})()&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ emit('?->', true, $okLabel, undefined, undefined); var r = borrowForCall(); /* store the assert-string constant */ makeString('a', $str._, $exprText, _s, _i); /* argument goes into %<r+1> */ makeArgValue($str._, r + 1); /* call the failure handler */ emit('()', '?', '^assertFail', '%' + r, '*1'); /* tidy temporaries */ returnBack('%' + (r + 1)); returnBack('%' +  r); /* continue after OK-label */ emit('<-?', true, $okLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Block($){return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while(Statement($));})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Goto($){var $label=createParserContext();return (function(){var _b=_i;return GOTO($)&&_($)&&Identifier($label)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ emit('-->', undefined, '@' + $label._, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function If($){var $dontLabel,$doneLabel;return (function(){var _b=_i;return IF($)&&_($)&&BoolGroup($)&&(function(){ $dontLabel = newLabel('f'); emit('?->', false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){var _b=_i;return ELSE($)&&_($)&&(function(){ $doneLabel = newLabel('e'); emit('-->', undefined, $doneLabel, undefined, undefined); emit('<-?',  false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('<--', undefined, $doneLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ emit('<-?', false, $dontLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DoWhile($){var $loopLabel;return (function(){var _b=_i;return DO($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<-?', false, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&WHILE($)&&_($)&&BoolGroup($)&&(function(){ emit('?->', true, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Loop($){var $loopLabel;return (function(){var _b=_i;return LOOP($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function For($){var $var=createParserContext(),$gotInit,$init=createParserContext(),$toExpr=createParserContext(),$type,$to,$noLoopLabel,$loopLabel,$body=createParserContext();return (function(){var _b=_i;return FOR($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Variable($var)&&(function(){ /* loop-variable must be local, modifiable int / pointer */ var varMeta = metaSlot($var._); if (varMeta.operator !== '=' || span(varMeta.type, "ip") === 0) { fail( 'For variable must be a local modifiable int or pointer variable', _s, _i , 'E305'); } $gotInit = false;            /* flag to detect an explicit start value */ ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($init)&&(function(){ assign($init._, $var._, $init._, _s, _i); $gotInit = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&TO($)&&_($)&&Expr($toExpr)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var varMeta  = metaSlot($var._); var toMeta   = metaSlot($toExpr._); if (toMeta.type !== varMeta.type) { typeError( 'Incompatible types ({$type1} and {$type2})', _s, _i, varMeta.type, toMeta.type , 'E301'); } /* constant upper bound */ $to = makeRValue(toMeta); /* initial comparison  (var < to)                         */ emit( '<', toMeta.type, undefined, $gotInit ? metaSlot($init._).operands[1]     /* start value from “var = expr” */ : varMeta.operands[1],            /* or the original variable */ $to ); if ($gotInit) { releaseMeta($init._); } /* branch-out   and  loop label */ $noLoopLabel = newLabel('e'); emit('?->', false, $noLoopLabel, undefined, undefined); $loopLabel   = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($body)&&(function(){ var varMeta = metaSlot($var._); /* increment + jump back */ emit( '...', varMeta.type, varMeta.operands[1],        /* address of loop variable */ $to, $loopLabel ); emit('<-?', false, $noLoopLabel, undefined, undefined); returnBack($to); releaseMeta(varMeta); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Copy($){var $l=createParserContext(),$f=createParserContext(),$t=createParserContext(),$length,$type,$;return (function(){var _b=_i;return COPY($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($l)&&FROM($)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var fromMeta = metaSlot($f._); var toMeta   = metaSlot($t._); $length = makeConstant($l._, 'i', _s, _i); var lengthHash = dropHash($length); if (fromMeta.type + toMeta.type !== 'pp') { returnBack($length); typeError( 'Invalid types ({$type1} and {$type2})', _s, _i, fromMeta.type, toMeta.type , 'E301'); } var copyMeta = metaSlot($l._); makeMeta( copyMeta, 'copy', '?', makeRValue(toMeta, '&$%'), makeRValue(fromMeta, '&$%'), '*' + lengthHash ); emitMeta(copyMeta); returnBack($length); releaseMeta(copyMeta); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Destructure($){var $call=createParserContext();return (function(){var _b=_i;return (function(){ if (!dry) destTargets = []; ; return true})()&&DestTarget($)&&((function(){for(var _n=0;(function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&DestTarget($)||(_im=(_i>_im?_i:_im),_i=_b,false)})();++_n);return _n>0})())&&(_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){ destructuring = true; ; return true})()&&Expr($call)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ destructuring = false; if (!dry) finishDestructure($call._, destTargets, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DestTarget($){var $tgtGlobal,$id=createParserContext();return (function(){var _b=_i;return (function(){ $tgtGlobal = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $tgtGlobal = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&Identifier($id)&&(function(){ if (!dry) destTargets.push({ name: $id._, isGlobal: $tgtGlobal, code: _s, offset: _i }); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Switch($){var $f=createParserContext(),$t=createParserContext(),$size,$switcher,$,$switchExit,$progress,$stmt=createParserContext();return (function(){var _b=_i;return SWITCH($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s.substr(_i,2)==="==")&&(_i+=2,true)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(function(){ var switchMeta = metaSlot($._); /* the switch expression must be an int */ if (switchMeta.type !== 'i') { fail('Switch expression needs to be int', _s, _i, 'E306'); } /* lower bound (compile-time constant) */ switchMeta.from = makeConstant($f._, 'i', _s, _i); /*    size = to - from   */ $size = subConstInt( makeConstant($t._, 'i', _s, _i), switchMeta.from ); /*   switcher = (expr − from)   */ $switcher = subConstInt( makeRValue(switchMeta, '$%'), switchMeta.from ); switchMeta.switchLabel = newLabel('s'); $switchExit              = newLabel('e'); switchStack.push(switchMeta); emit( '-->#', switchMeta.type, $switcher, '*' + dropHash($size), switchMeta.switchLabel ); returnBack($switcher); returnBack($size); $progress = undefined;       /* track case / default presence */ ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return CASE($)&&_($)&&(function(){ /* multiple CASE groups -> fall-through handled here */ if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } else { $progress = 'gotCases'; } /* dump the literal “case ...” comment */ var snippet = _s.substr(_i); var pos     = find(snippet, ":\r\n"); if (pos >= 0) { snippet = snippet.substr(0, pos); } emit( ';', undefined, 'case ' + snippet, undefined, undefined ); ; return true})()&&CaseExpr($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&CaseExpr($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DEFAULT($)&&_($)&&(function(){ if ($progress === 'gotDefault') { fail('Default case already defined', undefined, undefined, 'E409'); } else if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } var ctx = switchStack[switchStack.length - 1]; emit(';',    undefined, 'default',       undefined, undefined); emit('<--',  undefined, ctx.switchLabel,  undefined, undefined); $progress = 'gotDefault'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(_s[_i]===":")&&(++_i,true)&&_($)&&Statement($stmt)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ var ctx = switchStack.pop() || metaSlot($._); /* no explicit “default” -> hook it up now                        */ if ($progress !== 'gotDefault') { emit('<--', undefined, ctx.switchLabel, undefined, undefined); } emit('<--', undefined, $switchExit, undefined, undefined); returnBack(ctx.from); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CaseExpr($){var $n;return (function(){var _b=_i;return Expr($)&&(function(){ /* offset = constant(expr) - switch.from                         */ var ctx      = switchStack[switchStack.length - 1]; var caseMeta = metaSlot($._); var baseFrom = (ctx ? ctx.from : caseMeta.from); var baseLabel = (ctx ? ctx.switchLabel : caseMeta.switchLabel); $n = subConstInt( makeConstant(caseMeta, 'i', _s, _i), baseFrom ); /* create label for this case                                     */ emit( '<--', undefined, baseLabel + '#' + dropHash($n), undefined, undefined ); returnBack($n); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function While($){var $loopLabel,$exitLabel;return (function(){var _b=_i;return WHILE($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&BoolGroup($)&&(function(){ $exitLabel = newLabel('e'); emit('?->', false, $exitLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); emit('<-?', false, $exitLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Value($){var $base=createParserContext(),$f=createParserContext(),$i=createParserContext(),$s=createParserContext();return (function(){var _b=_i;return Group($)||(_im=(_i>_im?_i:_im),_i=_b,false)||SIZEOF($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&TypeBase($base)&&(function(){ if (!dry) { var head = descHead($base._); var words = (isStructAtom(head) ? structWords(head) : 1); if (words === undefined) fail('sizeof of incomplete struct ' + head, _s, _i, 'E419'); makeMeta($._, ':=', 'i', undefined, '#' + words, undefined); setElem($._, undefined); } ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FloatLiteral($f)&&(function(){ if (!dry) { makeMeta($._, ':=', 'f', undefined, '#' + $f._, undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||IntegerLiteral($i)&&(function(){ if (!dry) { makeMeta($._, ':=', 'i', undefined, '#' + $i._, undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||StringLiteral($s)&&(function(){ if (!dry) { makeString('s', $._, evaluate($s._), _s, _i); setElem($._, 'i');      /* string data is int words (Impala 2) */ } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULL($)&&_($)&&(function(){ if (!dry) { makeMeta($._, ':=', 'p', undefined, '&NULL', undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULLFUNC($)&&_($)&&(function(){ if (!dry) { makeMeta($._, ':=', 'F', undefined, '&NULL', undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Variable($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Variable($){var $global,$id=createParserContext();return (function(){var _b=_i;return (function(){ $global = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $global = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&Identifier($id)&&(function(){ if (!dry) { lookup($._, $id._, $global, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Identifier($){return (function(){var _b=_i;return (function(){var _l=_i,_x=KEYWORD($);_i=_l;return !_x})()&&(function(){var _m=_i;return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$".indexOf(_s[_i])>=0)&&(++_i,true)&&((function(){while(SYMBOL_CHAR($));})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FloatLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&(_s[_i]===".")&&(++_i,true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&((function(){var _b=_i;return (function(){var _b=_i;return (_s[_i]==="E")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="e")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IntegerLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&(function(){var _b=_i;return (_s.substr(_i,2)==="0x")&&(_i+=2,true)&&((function(){for(var _n=0;HEX($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="'")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s[_i]==="'")&&(++_i,true);_i=_l;return !_x})()&&ASCII($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StringLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="\"")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\"\\\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="\\")&&(++_i,true)&&(function(){var _b=_i;return (!!_s[_i]&&"\"\\bfnrt".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="u")&&(++_i,true)&&HEX($)&&HEX($)&&HEX($)&&HEX($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="\"")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function KEYWORD($){var _b=_i,_words=KEYWORD_WORDS,_word,_end,_x;for(var _k=0;_k<_words.length;++_k){_word=_words[_k];if(_s.substr(_i,_word.length)===_word){_i+=_word.length;_end=_i;_x=SYMBOL_CHAR($);_i=_end;if(!_x)return true;_i=_b;}}_im=(_i>_im?_i:_im);_i=_b;return false}
function ABS($){return (function(){var _b=_i;return (_s.substr(_i,3)==="abs")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ARRAY($){return (function(){var _b=_i;return (_s.substr(_i,5)==="array")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASSERT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="assert")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CASE($){return (function(){var _b=_i;return (_s.substr(_i,4)==="case")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CONST($){return (function(){var _b=_i;return (_s.substr(_i,5)==="const")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COPY($){return (function(){var _b=_i;return (_s.substr(_i,4)==="copy")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DEFAULT($){return (function(){var _b=_i;return (_s.substr(_i,7)==="default")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DO($){return (function(){var _b=_i;return (_s.substr(_i,2)==="do")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ELSE($){return (function(){var _b=_i;return (_s.substr(_i,4)==="else")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function EXPORT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="export")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IMPORT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="import")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function EXTERN($){return (function(){var _b=_i;return (_s.substr(_i,6)==="extern")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FLOAT($){return (function(){var _b=_i;return (_s.substr(_i,5)==="float")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FLOOR($){return (function(){var _b=_i;return (_s.substr(_i,5)==="floor")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FOR($){return (function(){var _b=_i;return (_s.substr(_i,3)==="for")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FROM($){return (function(){var _b=_i;return (_s.substr(_i,4)==="from")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FTOI($){return (function(){var _b=_i;return (_s.substr(_i,4)==="ftoi")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCPTR($){return (function(){var _b=_i;return (_s.substr(_i,7)==="funcptr")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCTYPE($){return (function(){var _b=_i;return (_s.substr(_i,8)==="functype")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FUNCTION($){return (function(){var _b=_i;return (_s.substr(_i,8)==="function")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GLOBAL($){return (function(){var _b=_i;return (_s.substr(_i,6)==="global")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GOTO($){return (function(){var _b=_i;return (_s.substr(_i,4)==="goto")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IF($){return (function(){var _b=_i;return (_s.substr(_i,2)==="if")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function INT($){return (function(){var _b=_i;return (_s.substr(_i,3)==="int")&&(_i+=3,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ITOF($){return (function(){var _b=_i;return (_s.substr(_i,4)==="itof")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LOCALS($){return (function(){var _b=_i;return (_s.substr(_i,6)==="locals")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LOOP($){return (function(){var _b=_i;return (_s.substr(_i,4)==="loop")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function NATIVE($){return (function(){var _b=_i;return (_s.substr(_i,6)==="native")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function NULL($){return (function(){var _b=_i;return (_s.substr(_i,4)==="null")&&(_i+=4,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function NULLFUNC($){return (function(){var _b=_i;return (_s.substr(_i,8)==="nullfunc")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function POINTER($){return (function(){var _b=_i;return (_s.substr(_i,7)==="pointer")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function READONLY($){return (function(){var _b=_i;return (_s.substr(_i,8)==="readonly")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function RETURNS($){return (function(){var _b=_i;return (_s.substr(_i,7)==="returns")&&(_i+=7,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SWITCH($){return (function(){var _b=_i;return (_s.substr(_i,6)==="switch")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SIZEOF($){return (function(){var _b=_i;return (_s.substr(_i,6)==="sizeof")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function STRUCT($){return (function(){var _b=_i;return (_s.substr(_i,6)==="struct")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TEMPORARY($){return (function(){var _b=_i;return (_s.substr(_i,9)==="temporary")&&(_i+=9,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TO($){return (function(){var _b=_i;return (_s.substr(_i,2)==="to")&&(_i+=2,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function WHILE($){return (function(){var _b=_i;return (_s.substr(_i,5)==="while")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BITWISE_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,2)==="<<")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,3)===">>>")&&(_i+=3,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)===">>")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="|")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="|")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="&")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="^")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ADDSUB_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="+")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="-")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MULDIV_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="/")&&(++_i,true)&&(function(){var _l=_i,_x=(_s[_i]==="/")&&(++_i,true);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="%")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COMP_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,2)==="<=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)===">=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="!=")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="==")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="<")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===">")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BASE_TYPE($){var $;return (function(){var _b=_i;return INT($)&&(function(){ $._ = 'int'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||FLOAT($)&&(function(){ $._ = 'float'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||FUNCPTR($)&&(function(){ $._ = 'funcptr'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||POINTER($)&&(function(){ $._ = 'pointer'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BUILT_IN($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s.substr(_i,3)==="abs")&&(_i+=3,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,5)==="floor")&&(_i+=5,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,4)==="itof")&&(_i+=4,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,4)==="ftoi")&&(_i+=4,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PREFIX_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="-")&&(++_i,true)&&(function(){var _l=_i,_x=(function(){var _b=_i;return (_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DIGIT($)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="~")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||BUILT_IN($)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SYMBOL_CHAR($){return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function HEX($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789ABCDEFabcdef".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DIGIT($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASCII($){return (function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function _($){return (function(){var _b=_i;return ((function(){while((function(){var _b=_i;return ((function(){for(var _n=0;(!!_s[_i]&&" \t\r\n".indexOf(_s[_i])>=0)&&(++_i,true);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||COMMENT($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function COMMENT($){return (function(){var _b=_i;return (_s.substr(_i,2)==="/*")&&(_i+=2,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s.substr(_i,2)==="*/")&&(_i+=2,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s.substr(_i,2)==="*/")&&(_i+=2,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s.substr(_i,2)==="//")&&(_i+=2,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\r\n".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function createParserContext() {
        return {
                _: { operator: undefined, type: undefined, operands: [ undefined, undefined, undefined ] }
        };
}
var _i=0,_im=0,_o=createParserContext();
_o.options=_hostOptions;
var _b=root(_o);
return [_b,_o._,(_b?_i:_im)];
});
function impalaCompiler(source, options) {
	var compilerOptions;
	if (typeof options === 'string') {
		compilerOptions = { sourceName: options };
	} else if (options) {
		compilerOptions = options;
	} else {
		compilerOptions = {};
	}
	return impalaCompilerImpl(source, compilerOptions);
}
if (typeof module !== 'undefined' && module.exports) {
	module.exports = impalaCompiler;
	module.exports.impalaCompiler = impalaCompiler;
	module.exports.default = impalaCompiler;
	module.exports.raw = impalaCompilerImpl;
}
