/* GENERATED from impala/impala.jspeg by `node impala/updateJSPEG.js` -- do not edit by hand. */
var $$parser = {};
var impalaCompilerImpl = (function(_s, _options) {
var _hostOptions = _options || {};
var KEYWORD_WORDS = [
	'abs', 'array', 'assert', 'case', 'const', 'copy', 'default', 'do', 'else', 'export', 'extern',
	'float', 'floor', 'for', 'from', 'ftoi', 'funcptr', 'functype', 'function', 'global', 'goto', 'if',
	'import', 'inline', 'int', 'itof', 'locals', 'loop', 'native', 'null', 'nullfunc', 'pointer', 'readonly',
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
    function find(str, chars, from) {                             /// index of the first `chars` at or after `from`
        var i = from || 0;
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
    var IMPALA_VERSION = '2.0';
    var dry            = false;
    var legacyMode     = (typeof _hostOptions !== 'undefined' && _hostOptions != null
            && !!_hostOptions.legacy);                          /// `--legacy` downgrades strict-expression errors to warnings
    var rangeChecks    = (typeof _hostOptions !== 'undefined' && _hostOptions != null
            && !!_hostOptions.rangeChecks);                     /// `--range-checks` emits DEBUG-gated runtime bounds tests
    var units          = ((typeof _hostOptions !== 'undefined' && _hostOptions != null
            && _hostOptions.units) || undefined);               /// import-closure spans {name,start,end}[], for origins
    var declOffset     = 0;                            /// offset of the declaration being parsed - see `root`
    var declSource     = '';                           /// its source text, so deferred checks can still render a caret

    /* 2  make sure the buckets exist */
    var META_TO_GAZL   = {};
    var SUPPORTED_OPS  = {};
    var CASTS_TO_TYPES = {};
    var ZEROES = {};
    var TYPE_SUFFIXES  = {};
    var VERBOSE_TYPES  = {};
    /* Read from `_hostOptions` HERE, the way `units` and `rangeChecks` above do. `var x`
       generates a CLOSURE LOCAL, so a plain `= undefined` here shadowed the `sourceName` the
       generated header assigns from the host - every read saw undefined and no single-unit row ever
       carried its file name, while multi-unit rows got one through `units` and looked fine. */
    var sourceName     = ((typeof _hostOptions !== 'undefined' && _hostOptions != null
            && _hostOptions.sourceName) || undefined);          /// names a single unit's rows; `units` names a closure's
    var metacode = [];
    var strings = { s:[], a:[] };
    var labelCounter = 0;
    var stock = { '%': [], '<': [] };
    var counters = { '%': 0,  '<': 0  };
    var symbols = { 'locals': {}, 'globals': {}, 'functions': {}, 'defines': {} };
    var structs = {};                                    /// name -> { fields:[{name,type,elem,offset,words}], words, complete }
    var openStruct = undefined;                          /// struct whose field list is being parsed (its fields own their extent scratches)
    var externArray = false;                             /// a top-level `extern array` is being declared - host-owned, like an extern struct field
    var functypes = {};                                  /// name -> signature { params, returnList, returnCount, returns, returnWords, complete }
    var topNames = {};                                   /// name -> kind ('global'/'function'/'const'/'struct'/'functype'), one flat namespace
    var guardCounter = 0;                                /// mints `.g<N>` skip labels for deferred assertions; NOT labelCounter, which resets per function
    var emittedGuards = {};                              /// (array, index) pairs already asserted in this function - the same assertion twice says nothing new
    var holdCounter = 0;                                 /// names minted by holdConstant when the scratch pool ran dry
    var initTarget = undefined;                          /// element DESCRIPTOR an `InitList`'s entries answer to; undefined = untyped array, accept what comes
    var reservedAt = undefined;                          /// offset of the last name reported as reserved - two doors check one name, one reports it
    var switchStack = [];
    var noForward = false;
    var exportNext = false;                              /// set while parsing an `export`-prefixed declaration (Step 5)

    /* 3  bulk-fill the lookup tables */
    map(META_TO_GAZL,
        '=',   'MOV?', ':=',  'MOV?',
        '=itof','iTOf', '=ftoi','fTOi', '=abs','ABS?', '=floor','FLOf',
        '=[]','PEEK',  '[]=','POKE',   '=[]$','GETL',  '[]$=','SETL',
        '=*','PEEK', ':=*','PEEK', '*=','POKE', '=&','ADRL', 'copy','COPY',
        '-->','GOTO', '-->#','SWCH',  '...','FOR?',  '()','CALL', '--^','RETU', 'FAIL','FAIL',
        '|','IOR?', '&','AND?', '^','XOR?', '<<','SHL?', '>>>','SHRu', '>>','SHR?',
        '+','ADD?', '-','SUB?', '*','MUL?', '/','DIV?', '%','MOD?', 'd','DIFp',
        '<=','LEQ?', '<','LSS?', '>=','GEQ?', '>','GRT?', '!=','NEQ?', '==','EQU?',
        '!<=','GRT?','!<','GEQ?','!>=','LSS?','!>','LEQ?','!!=','EQU?','!==','NEQ?'
    );

    map(SUPPORTED_OPS,
        '=*p','?', '=&f','p', '=&t','p', '=&i','p', '=&p','p', '=&?','p',
        '=-i','i', '=-f','f', '=~i','i',
        '=floatf','f', '=float?','f', '=funcptrt','t', '=funcptr?','t',
        '=inti','i', '=int?','i', '=pointerp','p', '=pointer?','p',
        '=absi','i', '=absf','f', '=itofi','f', '=ftoif','i', '=floorf','f',
        '|ii','i', '&ii','i', '^ii','i', '<<ii','i', '>>>ii','i', '>>ii','i',
        '+ii','i', '-ii','i', '*ii','i', '/ii','i', '%ii','i',
        '+ff','f', '-ff','f', '*ff','f', '/ff','f',
        '+pi','p', '-pi','p', '-pp','i', '-tt','i',   /* funcptr difference: ordinal distance, DIFp (DIFt in GAZL 2) */
        '=[]pi','?',
        '<=ii','i', '<ii','i', '>=ii','i', '>ii','i', '!=ii','i', '==ii','i',
        '<=ff','f', '<ff','f', '>=ff','f', '>ff','f', '!=ff','f', '==ff','f',
        '<=pp','p', '<pp','p', '>=pp','p', '>pp','p', '!=pp','p', '==pp','p',
        '<=tt','t', '<tt','t', '>=tt','t', '>tt','t', '!=tt','t', '==tt','t'
    );

    map(CASTS_TO_TYPES, 'float','f','funcptr','t','int','i','pointer','p');
    map(ZEROES,         'f','#0.0','i','#0','p','&NULL','t','&NULL');
    /* GAZL 2: `'t','p'` is where Impala THROWS AWAY the funcptr/data-pointer distinction it already has -
       GAZL 1 offers only `p` for both, so a funcptr array and a pointer array emit identical `DATp` rows.
       When GAZL 2 lands the `t` (target) type this becomes `'t','t'` and Impala needs nothing else; the
       type is already tracked. See design/gazl/GAZL2FunctionPointers.md. */
    map(TYPE_SUFFIXES,  'void','', 'i','i','f','f','p','p','t','p','U','',
                                 'N','',   'A','A','?','E', 'V','');
    map(VERBOSE_TYPES,  'i','int','f','float','p','pointer','t','funcptr',
                                 'U','function','N','native','A','array','S','struct','?','untyped',
                                 'V','void');

    function signatureParamCategory(type) {
        switch (type) {
            case 'i': return 'int';
            case 'f': return 'float';
            case 'p': return 'ptr';
            case 't': return 'funcptr';
            default:  return 'unknown';
        }
    }

    /* ONE walk over a descriptor, in two vocabularies: the `; signature` metadata token
       ("int-ptr") and the prose a diagnostic reads ("int pointer"). They differ only in what joins a
       pointer level, which table names a scalar, and what an absent descriptor is called - so they
       share the recursion and a change to the descriptor grammar cannot update one and forget the
       other. */
    function renderDesc(desc, absent, ptrJoin, leaf) {
        if (desc === undefined) return absent;
        var head = descHead(desc), tail = descTail(desc);
        if (head === 'S') return tail;                            /* struct: the tail IS its name */
        if (head === 't') return (tail !== undefined ? tail : 'funcptr');   /* named type, or a bare funcptr */
        if (head === 'p' && tail !== undefined) return renderDesc(tail, absent, ptrJoin, leaf) + ptrJoin;
        return leaf(head);
    }

    function signatureCategoryForDesc(desc) {                     /// 'pi'->"int-ptr", 'pSFilter'->"Filter-ptr"
        return renderDesc(desc, 'unknown', '-ptr', signatureParamCategory);
    }

    /* full descriptor of a declared symbol: type char + optional element chain */
    function fullDescFor(type, elem) {
        return (elem !== undefined ? type + elem : type);
    }

    function signatureReturnCategory(type, known) {               /* the param table, plus what only a RETURN can be */
        if (type === 'V') return 'void';
        if (type === '?') return (known ? 'void' : 'unknown');
        return signatureParamCategory(type);
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

    function renderTypeList(typeCodes, elems) {
        if (!typeCodes || typeCodes.length === 0) {
            return '';
        }

        var parts = [];
        for (var i = 0; i < typeCodes.length; ++i) {
            parts.push(typeCodes[i] === 'S' && elems && elems[i] !== undefined
                    ? elems[i]                              /* by-value struct: the struct name itself */
                    : signatureCategoryForDesc(fullDescFor(typeCodes[i], elems ? elems[i] : undefined)));
        }
        return parts.join(', ');
    }

    /* Types only, no names: what a signature CLAIM actually asserts, and the only part two claims
       about one name have to agree on. Element chains stay in - a signature-row consumer treats a bare `ptr` as
       element-UNKNOWN and lets it match any chain, so flattening `V-ptr` to `ptr` would silently
       disarm the cross-unit check. That is the only type record a name-only `extern function f` has. */
    function renderParamTypes(params) {
        var parts = [], count = (params ? params.length : 0);
        for (var idx = 0; idx < count; ++idx) {
            var param = params[idx] || {};
            parts.push(param.type === 'S' ? (param.struct || 'unknown')
                    : signatureCategoryForDesc(fullDescFor(param.type, param.elem)));
        }
        return parts.join(', ');
    }

    /* The comparable shape of anything callable - a function signature, an extern prototype or a
       `functype`, which all carry the same params/returns fields. */
    function signatureShape(name, sig) {
        return name + '(' + renderParamTypes(sig.params) + ') -> ' + renderReturnType(sig);
    }

    /* Two claims about one name that do not agree - `got` arrived second. A DEFINITION outranks a
       declaration, so it is named as authoritative where the closure has one; with none in sight the
       claims are only judgeable against each other, and blaming a definition would cite no file. */
    function failDisagreement(what, got, had, code, priorIsDefinition, noun, plural, sourceCode, sourceOffset) {
        if (priorIsDefinition) {
            fail('extern declaration of ' + what + ' does not match its definition: declared "'
                    + got + '" but definition provides "' + had + '"', sourceCode, sourceOffset, code,
                    'the definition is authoritative - correct the declaration, or drop it and let the definition speak');
        }
        fail('extern declarations of ' + what + ' disagree: "' + got + '" vs "' + had + '"',
                sourceCode, sourceOffset, code,
                'one ' + noun + ' cannot have two ' + plural
                        + ' - make the declarations identical, or keep only one');
    }

    function renderReturnType(signature) {
        if (signature.returnCount > 1) {                           /* multiple return values -> (t1, t2, ...) */
            var parts = [];
            for (var idx = 0; idx < signature.returnList.length; ++idx) {
                var r = signature.returnList[idx];
                parts.push(r.elem !== undefined ? signatureCategoryForDesc(fullDescFor(r.type, r.elem))
                        : signatureParamCategory(r.type));
            }
            return '(' + parts.join(', ') + ')';
        }
        if (signature.returns === 'S') {                           /* by-value struct return -> the struct name */
            return (signature.returnStruct || 'unknown');
        }
        return (signature.returnElem !== undefined
                ? signatureCategoryForDesc(fullDescFor(signature.returns, signature.returnElem))
                : signatureReturnCategory(signature.returns, signature.returns !== undefined));
    }

    /* Compiling an import closure means ONE concatenated source, so a raw offset gives a line that
       indexes the concatenation and a name that is always the root unit - the wrong file, on the
       wrong line, for everything past the first. The spans recorded by the closure walk map the
       offset back to the unit that owns it. A lone unit is left exactly as it was: no name, no
       shift, so a source that imports nothing keeps emitting the bytes it always did. */
    function originUnit(offset) {                                 /* no local alias: `var units = units` rewrites to `var units = units` */
        if (!units || units.length <= 1) {
            return undefined;
        }
        for (var i = 0; i < units.length; ++i) {
            if (offset >= units[i].start && offset <= units[i].end) {
                return units[i];
            }
        }
        return undefined;
    }

    /* Callers ask for positions in roughly SOURCE ORDER, so the walk resumes from the previous answer
       rather than restarting at the unit. Without this it is O(offset) per call and quadratic over a
       file - measured on adventCode.impala (102KB): 742 calls scanning 33.8M characters. A backward
       jump, a different unit or a different source falls back to a full scan, so the result never
       depends on call order. `idx` (not `offset`) is memoised because the CRLF arm can step one past
       the target, and it is the position `line`/`column` actually describe. */
    var originMemo = null;

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

        var unit = originUnit(offset);
        var from = (unit ? unit.start : 0);
        var line = 1;
        var column = 1;
        var idx = from;
        if (originMemo !== null && originMemo.code === sourceCode && originMemo.from === from
                && originMemo.at <= offset) {
            idx = originMemo.at; line = originMemo.line; column = originMemo.column;
        }
        for (; idx < offset; ++idx) {
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
        originMemo = { code: sourceCode, from: from, at: idx, line: line, column: column };

        var origin = line + ':' + column;
        var name = (unit ? unit.name : sourceName);
        if (name) {
            origin = name + ':' + origin;
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
        var returnType = renderReturnType(signature);
        var kind = (role ? role : 'func');
        if (exportNext) kind = 'export ' + kind;         /* Step 5: host-visible marker */

        var originName = (sourceName !== undefined ? sourceName : (signature ? signature.sourceName : undefined));
        var originCode = (sourceCode !== undefined ? sourceCode : (signature ? signature.sourceCode : undefined));
        var originOffset = (sourceOffset !== undefined ? sourceOffset : (signature ? signature.sourceOffset : undefined));

        return appendOrigin('signature ' + kind + ' ' + name + '(' + paramsText + ') -> ' + returnType,
                            originName, originCode, originOffset);
    };

    /* Takes the call's own record rather than eight positional arguments. That list used to be written
       three times - here, at the emit site, and again as `callInfo.commentArgs`, the cache the REFRESH
       replays it from - so adding or reordering a field meant editing three parallel lists, and
       forgetting the cache made the refreshed `; expects` row disagree with the emitted one. */
    formatCallExpectationComment = function (call, callResultType) {
        var signature = call.signature;
        var label = (call.name || 'function');
        var paramsText = (signature && signature.params ? renderParamTypes(signature.params)
                                   : renderTypeList(call.actualTypes, call.actualElems));

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
                : (returnCode === 'p' && signature && signature.returnElem !== undefined
                        ? signatureCategoryForDesc(fullDescFor(returnCode, signature.returnElem))
                        : signatureReturnCategory(returnCode, known)));

        return appendOrigin('expects ' + label + '(' + paramsText + ') -> ' + returnType,
                            call.sourceName, call.sourceCode, call.sourceOffset);
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

        var refreshed = formatCallExpectationComment(args, callResultType);

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

    /* Called once per `function` DEFINITION, to emit the `name: FUNC` header. `kind === 'FUNC'` means a
       header was already emitted for this name, i.e. the name is being defined twice - which used to
       return silently, so the second body was emitted with NO header and the assembler reported the
       baffling "Parameters and locals must be declared at top of FUNC". Two units of an import closure
       defining the same function is the usual way in. */
    emitFunctionSignature = function (name, sourceCode, sourceOffset) {
        var entry = symbols.functions[name];
        if (!entry) {
            return;
        }
        if (entry.kind === 'FUNC') {
            fail('Function ' + name + ' is already defined',
                    sourceCode, sourceOffset, 'E401',
                    'each function needs a unique name across the whole import closure');
        }
        checkExternAgreement(name, entry.externProto, entry.signature, true, sourceCode, sourceOffset);

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

    /* One name may have at most one DEFINITION but any number of `extern` declarations, so long as
       every claim agrees: with the definition if the closure has one, and otherwise with each other.
       This does NOT police linkage - a prototype for a name nothing defines is a promise only
       a signature-row consumer can settle, and a name-only extern records no prototype and so asserts nothing.
       It fires exactly when the compiler is holding two claims itself, which an import closure makes
       routine since the builder compiles the whole closure as one unit. Names are not compared, only
       types, so a consumer of the rows can compare two declarations of one name. */
    checkExternAgreement = function (name, claim, prior, priorIsDefinition, sourceCode, sourceOffset) {
        if (!claim || !prior) {
            return;
        }
        var got = signatureShape(name, claim), had = signatureShape(name, prior);
        if (got !== had) {
            failDisagreement(name, got, had, 'E437', priorIsDefinition, 'function', 'shapes',
                    sourceCode, sourceOffset);
        }
    };

    function signatureRoleForSection(section) {
        switch (section) {
            case 'CNST': return 'readonly';
            case 'TEMP': return 'temporary';
            default:     return 'global';
        }
    }

    /* `; signature` rows describe what Impala compiled against, in the emitted `.gazl` itself. They were
       written for `gazl-validate`, a separate cross-unit checker RETIRED 2026-08-05: `import` compiles the
       whole closure in one pass, so the compiler now catches cross-unit disagreement itself (`E438`, at
       the source, with a caret) and a host's native table is better written as Impala prototypes than as
       metadata a second tool compares - see `impala/natives.impala`. The rows stay because they document
       the module for a reader, and the format notes below still hold for anything that parses them.

       Declare an `extern struct` interface: the field names and types Impala compiled against, so the
       host-supplied layout (.o.Name.field / .z.Name) is stated rather than silently assumed. Rendered as:
           ; signature extern struct Name { field : int, other : float-ptr } @ 12:1        */
    /* An array extent in a signature row is a CLAIM a consumer compares against the other side. A
       literal or a single named const is a real claim; an extent that folded to a `<X>` compile-time
       scratch is not - the name is pool-recycled, so two unrelated extents both render `<A>` and
       compare EQUAL. Render those as the empty extent instead, the same wildcard a sizeless `extern
       array` already uses, which a consumer skips rather than trusting. A row may state a fact or
       state "unknown"; it must never state something that merely looks like a fact. */
    /* A SHAPE states every axis, in WRITTEN order (outermost first, the reverse of the stride order `dims`
       holds), joined by `x`: `int array cells[3, 4]` advertises `int[3x4]`. Deliberately not a comma - a
       `; signature struct` row is a comma-separated field list, and a signature-row consumer splits it on commas
       before it ever looks at a type. `x` keeps a shape one `\S+` token, so a consumer compares it as
       the opaque string it already compares a 1-D extent as, and needs no change at all.
       All-or-nothing, exactly as a single extent is: ONE unstatable axis and the whole shape reads `[]`,
       because a partial shape rendered as a whole one is a claim that is simply false. */
    function extentText(size, dims) {
        var text = (size === undefined ? '' : '' + size);
        if (dims !== undefined) {
            text = '';
            for (var i = dims.length - 1; i >= 0; --i) {
                /* A host-owned axis (undefined - the host knows it, we do not) renders `[]` for the same
                   reason a folded `<X>` does, and DELIBERATELY not `[,]`: a consumer compares extents
                   as raw strings and skips an empty one, so a rank claim would make a definition's
                   `int[3x4]` conflict with an extern's rank-2 row. Rank is checked on the Impala side. */
                if (dims[i] === undefined || ('' + dims[i]).charAt(0) === '<') return '[]';
                text += (i < dims.length - 1 ? 'x' : '') + dims[i];
            }
        }
        return '[' + (text.charAt(0) === '<' ? '' : text) + ']';
    }

    structSignatureRow = function (name, isExtern, sourceName, sourceCode, sourceOffset) {
        var s = structs[name];
        if (!s || !s.fields) {
            return undefined;
        }
        var parts = [];
        for (var i = 0; i < s.fields.length; ++i) {
            var f = s.fields[i];
            var cat;
            if (f.type === 'A') {                                 /* an array field advertises its extent + element */
                cat = signatureCategoryForDesc(f.elem) + extentText(f.size, f.dims);
            } else if (f.type === 'S') {
                cat = f.struct;                                   /* a nested by-value struct field */
            } else {
                cat = (f.elem !== undefined ? signatureCategoryForDesc(fullDescFor(f.type, f.elem))
                                            : signatureParamCategory(f.type));
            }
            parts.push(f.name + ' : ' + cat);
        }
        return appendOrigin('signature ' + (isExtern ? 'extern ' : '') + 'struct ' + name
                                    + ' { ' + parts.join(', ') + ' }',
                            sourceName, sourceCode, sourceOffset);
    };

    formatGlobalSignatureComment = function (section, name, type, size, flavor,
                                                      sourceName, sourceCode, sourceOffset, elem, dims) {
        if (!name) {
            return undefined;
        }

        var prefix = (exportNext ? 'export ' : '') + (flavor ? flavor + ' ' : '');

        if (type === 'A') {
            var extent = extentText(size, dims);                  /* a SHAPE states every axis: `g[3x4]` */
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

    /* `isExtern` marks a VALUELESS `const int N;` - external by omission of a value ("defined later by
       the GAZL run-time"). It spells no `extern` keyword, so it used to emit no row at all and was the
       one extern kind the rows could not describe (nothing emitted a row for it, so a consumer had
       nothing to compare). Emitting the row turns that pass on without touching the spelling. */
    formatConstSignatureComment = function (name, type, sourceName, sourceCode, sourceOffset, elem,
                                                     isExtern) {
        if (!name) {
            return undefined;
        }

        var prefix = (isExtern ? 'extern ' : (exportNext ? 'export ' : ''));
        return appendOrigin('signature ' + prefix + 'const ' + name + ' : '
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

        /* A `<-?` join may be DELETED only if this pass can still redirect every reference to it,
           and the backward walk can only do that for a reference it has NOT visited yet - one at a
           LOWER index. A reference from below (a `do`-while back-edge) had its operand fixed before
           the alias existed, and one from an operator the walk never rewrites (the `<> ==` guard an
           `assert` bakes its ok-label into) is never offered the alias at all. Either way the label
           has to stay, so work out which ones up front instead of sniffing the tag letter. */
        var pinned = {}, joinAt = {};
        for (var i = 0; i < metacode.length; ++i) {
            if (metacode[i].operator === '<-?') joinAt[metacode[i].operands[0]] = i;
        }
        for (var i = 0; i < metacode.length; ++i) {
            var rec = metacode[i];
            if (rec.operator === '<-?') continue;       // its own operand 0 is the definition, not a reference
            var rewritable = (rec.operator === '?->' || rec.operator === '-->');
            for (var k = 0; k < 3; ++k) {
                var at = joinAt[rec.operands[k]];
                if (at !== undefined && (!rewritable || i > at)) pinned[rec.operands[k]] = true;
            }
        }

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
                        inst.operator = (pinned[lbl] ? '<--' : null);
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

        /* POST-CONDITION. This pass OWNS every `@.` label - it mints them, aliases them and
           deletes them - so a reference left pointing at a name it also deleted is a bug HERE,
           and saying so beats the assembler's "Symbol not found" three layers downstream.
           A user label (`@name`) is not ours to promise, but it IS decidable: a label is always
           local to the function body, and by the time this runs the body is fully parsed, so the
           map below is the complete set. An undefined one is a user error, not an internal bug,
           so it gets a diagnostic at the `goto` rather than an assert. */
        var defined = {};
        for (i = 0; i < metacode.length; ++i) {
            if (metacode[i].operator !== '<--') continue;
            var name = metacode[i].operands[0];
            /* A user label (`@name`, never `@.` - those are ours and always unique) written twice
               mints two identical GAZL labels; the assembler then rejects "Symbol already defined"
               against a name and line the user never wrote. Decidable here - the map already exists. */
            if (defined[name] && name.charAt(1) !== '.' && metacode[i].labelOffset !== undefined) {
                fail('Duplicate label ' + name.substr(1),
                        metacode[i].labelSource, metacode[i].labelOffset, 'E446',
                        'a label may be defined once per function');
            }
            defined[name] = true;
        }

        /* BRANCH THREADING and RETURN DUPLICATION (design/gazl/GAZLAssemblerOptimizations.md items 4 and 5).
           The walk above resolves an alias AT VISIT TIME, so it is not transitive: a chain it meets in
           the wrong order survives. Here everything has settled, so following one is just a lookup.
              `GOTO @a` where `a:` leads to the function's RETU BECOMES that RETU - `RETU` takes no
           operands and does the same frame work wherever it stands, so this costs nothing but saves a
           dispatch on every execution. It is the common case, not a corner one: Impala has no `return`
           statement, so an early exit is a `goto` to the end label, which is where RETU sits.
              Branch THREADING (item 4) is not here, and the reason recorded until 2026-08-03 was WRONG.
           It was not "a label graph is unsafe to rewrite from inside the compiler". The cause, isolated
           by minimising `Priyome`: `! EQUi #DEBUG #0 @L` is NOT control flow. It tells the ASSEMBLER to
           stop emitting until it reaches `L`, so the target does not mean "continue here" - it delimits
           a REGION OF TEXT that will not exist. Thread it to a later label and the skipped region grows,
           swallowing the label DEFINITIONS inside it. Hence a symbol reported missing while plainly
           present in the listing, blamed on the NEXT function, where the scope closes with the forward
           reference still open. Two attempts failed to isolate that.
              Skipping records whose operator starts with `<> ` fixes it completely (verified: Priyome
           assembles, 0/87). It is still not here, for a duller reason - it then changes ONE line in the
           whole corpus, and that line is in a synthetic fixture. The backward walk above already
           collapses every chain that runs in the direction it scans, which is all of them in practice.
              The same reasoning bounds what a label transform may do at all: re-pointing a reference to a
           label at the SAME address moves no skip region and is safe, while threading to a LATER one is
           what grows the region. The removed coincident-label merge was the former (see
           design/gazl/GAZLAssemblerOptimizations.md); items 4 and 5 there are the latter. */
        var leadsTo = {};
        for (i = 0; i < metacode.length; ++i) {
            if (metacode[i].operator !== '<--') continue;
            for (var n = i + 1; n <= metacode.length; ++n) {
                var nx = (n < metacode.length ? metacode[n] : { operator: '--^' });
                if (nx.operator == null || nx.operator === ';' || nx.operator === '<--') continue;
                leadsTo[metacode[i].operands[0]] = (nx.operator === '--^');
                break;
            }
        }
        for (i = 0; i < metacode.length; ++i) {
            rec = metacode[i];
            if (rec.operator === '-->' && leadsTo[rec.operands[0]] === true) {
                rec.operator = '--^';                             /* the GOTO IS the return */
                rec.operands[0] = undefined;
            }
        }

        for (i = 0; i < metacode.length; ++i) {
            var op = (rec = metacode[i]).operator;
            if (op == null || op === ';' || op === '<--') continue;
            for (var k = 0; k < 3; ++k) {
                var ref = rec.operands[k];
                if (typeof ref !== 'string' || ref.charAt(0) !== '@' || defined[ref]) continue;
                if (ref.charAt(1) === '.') {
                    assert(false, "branch to deleted label " + ref + " from " + op);
                } else if (rec.gotoOffset !== undefined) {
                    fail('goto to undefined label ' + ref.substr(1),
                            rec.gotoSource, rec.gotoOffset, 'E445',
                            'a label is local to its function - define it as `' + ref.substr(1)
                                    + ': ;` in this body, or remove the goto');
                }
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
            /* THE POOL OWNS UPPERCASE `<A>`..`<Z>`, and that is the whole reason the hand-picked
               compile-time scratches are lowercase (`<a>` the struct-layout accumulator, `<t>` its
               element-size temp). Those two live across a whole layout block, outside the pool's
               borrow/return accounting, so a name the allocator can hand out would be clobbered by the
               next fold - and a struct layout routinely has a pooled scratch live at the same time
               (`! MULi <A> #23 #5` computing an extent while `<a>` accumulates the offset). The case
               is the only thing keeping the two namespaces apart; a new fixed scratch must be
               lowercase, and a new lowercase name must not already be taken. */
            var idx = counters['<']++;
            assert(idx < 26, "compile-time scratch pool exhausted (expression too complex)");
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

        /* A call window grows UPWARD from its base, so the base must sit above every live
           transient or the window would overlap one. The free pool is not always top-anchored:
           an out-of-order release (e.g. `a[i] = v` frees the index temp low while the value temp
           is kept high, or a destructure) can leave a freed hole below a live temp. When that
           happens - the highest allocated transient is live rather than free - mint a fresh slot
           above everything instead of reusing the hole. This is a latent allocator bug reachable
           in 1.0 too (`a[i] = v; b[i] = f(arg)` crashes the un-guarded allocator); the fix is
           byte-identical for the corpus, which never seats a call over a live temp. */
        var maxFree = -1;
        for (var _k = 0; _k < stk.length; ++_k) {
            var _v = parseInt(stk[_k].substr(1), 10);
            if (_v > maxFree) maxFree = _v;
        }
        if (maxFree < counters['%'] - 1) {
            return counters['%']++;
        }

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
            var idx = stockIndexOf(stk, '%' + number);
            assert(idx >= 0, "transient %" + number + " must exist in stock");
            stk.splice(idx, 1);
        }
    };

    /* where `op` sits in a stock bucket, or -1. Searched from the top, because the pool is a LIFO and
       a token just returned is the one most likely to be asked about. */
    function stockIndexOf(stk, op) {
        for (var i = stk.length - 1; i >= 0; --i) {
            if (stk[i] === op) return i;
        }
        return -1;
    }

    returnBack = function (op) {
        if (op == null) {
            return;
        }
        var c = op[0];

        /* A compile-time scratch carried as the trailing `<X>` token of a COMPOUND operand:
           `*<A>` (alloc size), `#<D>` (pointer offset), `&bank:<D>` / `$v:<D>` (base:offset), and
           `%<A>` (a symbolically indexed transient). This must be tested BEFORE the bare-token case:
           `%<A>` starts with '%' but is not a slot number, and pushing it into the transient stock
           both loses the scratch and poisons the pool with a token no allocator can hand out. */
        if (op.length > 3 && op.charAt(op.length - 1) === '>' && op.charAt(op.length - 3) === '<') {
            returnBack(op.substr(op.length - 3));
        }
        /* a bare transient / compile-time scratch */
        else if (c === '%' || c === '<') {
            var stk = stock[c];
            if (stockIndexOf(stk, op) < 0) stk.push(op);   // avoid dupes
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

    /* One shape for an empty slot, so the places that used to spell the literal cannot drift apart. */
    function newMetaSlot() {
        return { operator: undefined, type: undefined,
                 operands: [ undefined, undefined, undefined ] };
    }

    function metaSlot(node) {
        if (node == null || (typeof node !== 'object' && typeof node !== 'function')) {
            return newMetaSlot();
        }
        /* A PARSE NODE carries its meta in the value slot `_`; anything else IS the record. Only a node
           with no operands of its own can be the former, which is why the two tests belong together -
           asking them separately put an `operands === undefined` branch inside the `else` of
           `operands !== undefined`, where it could never be false, and duplicated the ensure block. */
        if (node.operands === undefined && Object.prototype.hasOwnProperty.call(node, '_')) {
            var slot = node._;
            if (!slot || slot.operands === undefined) {
                slot = newMetaSlot();
                node._ = slot;
            }
            return slot;
        }
        if (!Array.isArray(node.operands)) {                      /* absent or wrong type: give it three */
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

    createParserContext = function () {
        return {
            _: newMetaSlot()
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
        /* A value meta is never a place. Meta slots are pooled and reused, so clear any leftover
           place/window state from a previous use (e.g. `*p = v` leaves a struct place on the slot
           that `p` is later looked up into) - otherwise fieldAccess mis-reads `p` as a struct value. */
        rec.place     = false;
        rec.baseKind  = undefined;
        rec.base      = undefined;
        rec.offParts  = undefined;
        rec.arrayOf   = undefined;
        rec.extent    = undefined;
        rec.oobIndex  = undefined;
        rec.struct    = undefined;
        rec.dynIndex  = undefined;
        rec.readonly  = false;        /* pooled slots: never inherit a previous symbol's writability */
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
    /* A CONSTANT index against a KNOWN extent. One rule for every array, called from both subscript
       paths - a struct field keeps its extent on a place, a plain array decayed to a pointer and carries
       it on the meta, and there is no reason for `g[9]` and `s.v[9]` to report differently. Undecidable
       when either side is symbolic (`v[SN]`, a runtime index); those fall through to --range-checks. The
       A NEGATIVE index is caught here too: `v[-1]` folds to a legal-looking positive offset inside the
       struct and writes BACKWARDS into whatever field precedes the array.

       PAST THE END IS NOT REPORTED HERE, because whether it is an error depends on what happens next,
       and the rule is `design/impala/Impala2Review.md`'s: a DEREFERENCE with an out-of-range constant index is a
       guaranteed trap, so it is a compile error, while ADDRESS FORMATION (`&a[7]`, `&p[i]`) is always
       legal and never checked - an out-of-range address is a value like any other, and GAZL's own check
       fires on access operands, not on address-taking. So the finding is returned as a flag; reference()
       clears it and checkIndexUse reports on every use that actually reads or writes the element. That
       is also why Impala needs no one-past-the-end carve-out where C does; verified to `&g[1000000]`.

       BEFORE THE START is different, and fails outright - address or not. It is not "an address like any
       other": GAZL cannot even represent it on the direct form (`MOVp $p &g:-1` and `ADRL $p $a:-1` are
       both rejected at assembly, "Negative value not accepted"), and on the FOLDED form it is worse than
       rejected - `.o.S.pad + (-1)` is a perfectly good positive offset naming the field before it, so
       `&s.pad[-1]` assembles, runs, and aliases a neighbour. One source-level mistake had three different
       outcomes across the shapes; this is the one place that can give it one. */
    /* WHAT can decide this index, and WHEN. Every tier boundary reads this ONE table, because the three
       spellings of "constant" - `#123`, `#KONST` and a folded `<A>` - used to be sorted by three separate
       regexes in two functions whose union was never the whole operand universe, and each gap that left
       was found by hand after shipping rather than by a test. A new operand form now lands in a case that
       has to be named.

       A FOLDED scratch gets its OWN kind rather than being lumped in with either neighbour, because the
       two questions callers ask part company here. It is an assemble-time value, so it folds into an
       offset like any other constant. But `<A>` is recycled - the next folded subscript in the same
       function rebinds it (`! ADDi <A> #K #1` ... `! ADDi <A> #M #1`) - so it can never KEY a bounds
       assertion, which dedups by index text and would then answer for whichever value landed last.

       A COMPILER-MINTED name is as assemble-time as a user's: `#.d.g.0` is a layout constant the assembler
       resolves, and only the leading dot ever made it look otherwise. Reading it as `runtime` was the gap
       this table exists to close - it demoted whatever consumed it from `! MULi` to a real multiply, which
       is how `cube[1, 0, 0]` came to need a materializing `! MOVi` to get its tier back. The leading dot is
       the same mark `claimTopName` reads ("no Impala identifier starts with `.`"), minted only by
       extentSymbol / fieldSymbol / axisSymbol - change the scheme in those and this must follow.
       It also widens what `checkConstIndex` will DEFER on, which is safe only because foldAxes clears a
       shape's extent before a minted index can reach it. */
    indexKind = function (op) {
        if (/^#?<[A-Za-z]>$/.test(op)) return 'scratch';          /* an assemble-time fold under a recycled name */
        if (op.charAt(0) !== '#') return 'runtime';               /* $local, %transient */
        if (constInt(op) !== undefined) return 'now';    /* a literal - Impala decides it */
        return /^#\.?[A-Za-z_]/.test(op) ? 'assembly' : 'runtime'; /* #NAME - and #.z.NAME, #.d.NAME.k,
                                                                     #.o.S.f - are all the assembler's */
    };

    checkConstIndex = function (extent, idxRV, sourceCode, sourceOffset) {
        var kind = indexKind(idxRV);
        if (extent === undefined || kind === 'runtime' || kind === 'scratch') {
            return undefined;                                     /* tier 3's, if it is enabled at all */
        }
        if (kind === 'assembly') {
            /* A SYMBOLIC constant index - `a[KONST]`: the ASSEMBLER can evaluate the operand and Impala
               cannot. Decidable, just not now, so defer it exactly as a symbolic EXTENT is deferred
               below. Same scoping too: a plain array is caught natively (`&g:KONST` -> "Offset out of
               bounds: g"), a struct field is not. BOTH ends have to be asked, because neither is
               knowable here. */
            return extent.inField
                    ? { k: idxRV.substr(1), ext: extent, src: sourceCode, off: sourceOffset }
                    : undefined;
        }
        var ck = constInt(idxRV);
        if (ck < 0) {                                             /* decidable WITHOUT the extent */
            fail('Index ' + ck + ' is before the start of this array',
                    sourceCode, sourceOffset, 'E461',
                    'a negative index is not a legal address either');
        }
        var cn = constInt('#' + extent.n);
        if (cn === undefined) {
            /* TIER 2 - the extent is a symbol only the assembler can resolve. Deferred for a struct array
               FIELD, and ONLY there, because that is the only place nothing else looks: the overrun stays
               inside the struct's allocation, so `Symbols::resolve` sees a legal offset. A plain array
               with a symbolic extent is already caught natively (`a[7]` on `a[SN]` -> "Offset out of
               bounds: a"), and re-checking it here would put three assemble-time lines into the shipped
               text of the canonical `const int N; array a[N]` idiom to say what the assembler says for
               free - 15 of 87 goldens grew when this was not scoped. `stride` carries a struct-element
               field's element size, because `.z.` counts WORDS and the index counts elements. */
            if (!extent.inField) {
                return undefined;
            }
        } else if (ck < cn) {
            return undefined;                                     /* in range, and decided right here */
        }
        return { k: ck, n: cn, ext: extent, src: sourceCode, off: sourceOffset };
    };

    /* `--range-checks`: the DEBUG-gated RUNTIME bounds test for a subscript whose index is not a
       compile-time number, which is the only tier the two static ones cannot reach.

       TWO COMPARES AND NOTHING ELSE. The index is the only value not known until run time: the bound is
       the `.z.` SYMBOL, so it is an assemble-time immediate and needs no instruction to produce - which
       also makes this work unchanged for an extent the assembler resolves (`v[SN]`). For a struct element
       the operand handed in is the SCALED word offset, which is what `.z.` counts, and that multiply was
       already emitted for the access itself.

       An earlier version was branchless - `(extent - 1 - i) | i` has its sign bit set exactly when `i` is
       out of range, which is one conditional branch instead of two. That is the wrong trade: it spends
       three ALU instructions to save one branch - six runtime instructions against four, and FOUR against
       TWO on the path that matters, since an in-range access never reaches the failure call - and it
       computes `extent - 1` at RUN time from a constant the assembler already knows. GAZL fuses the
       compare and the branch, so each bound really is one instruction. Two compares also read as what
       they are.

       OFF BY DEFAULT, and that is not the same switch as `#DEBUG`. `DEBUG` decides whether the
       assembler EMITS the instructions; this decides whether they are in the .gazl TEXT at all - and
       the text is the shipped artifact, embedded into C++ via tools/gazlCompactor. Measured, an assert
       costs ~95 bytes of compacted source that `DEBUG 0` does not remove. */
    emitRangeCheck = function (idxOp, extent, sourceCode, sourceOffset) {
        if (!rangeChecks || extent === undefined
                || indexKind(idxOp) !== 'runtime') {
            return;                                               /* a constant, in any of its spellings,
                                                                     belongs to an earlier tier */
        }
        var ok  = beginDebugGuard('r');
        var bad = newLabel('r');
        emit('<', 'i', undefined, idxOp, '#0');           /* below the start -> fail */
        emit('?->', true, bad, undefined, undefined);
        emit('<', 'i', undefined, idxOp, '#' + extent.sym);   /* inside the extent -> ok */
        endDebugGuard(ok, {}, 'index out of range: ' + extent.what,
                sourceCode, sourceOffset, bad);
    };

    /* The two halves of a DEBUG-gated runtime check, shared by `assert` and by --range-checks so the
       protocol has one home: an assemble-time skip when `DEBUG` is 0, then (caller emits its comparison)
       a conditional jump past the failure call, the `^assertFail` call with its message constant, and the
       join. processBranches understands only this shape - the comparison must sit immediately before the
       `?->` - so a change to the assert-string protocol, assertFail's arity or the branch pair belongs
       here rather than in two places that must be kept bit-identical. `msg` is the meta slot the message
       constant is built into; `assert` passes its own so the string is its parsed expression text. */
    beginDebugGuard = function (tag) {
        var ok = newLabel(tag);
        emit('<> ==', 'i', '#DEBUG', '#0', ok);
        return ok;
    };

    endDebugGuard = function (ok, msg, text, sourceCode, sourceOffset, failLabel) {
        emit('?->', true, ok, undefined, undefined);
        if (failLabel !== undefined) {                            /* a second test that jumps straight to
                                                                     the failure lands here */
            emit('<-?', true, failLabel, undefined, undefined);
        }
        var r = borrowForCall();
        makeString('a', msg, text, sourceCode, sourceOffset);
        makeArgValue(msg, r + 1);
        emit('()', '?', '^assertFail', '%' + r, '*1');
        returnBack('%' + (r + 1));
        returnBack('%' + r);
        emit('<-?', true, ok, undefined, undefined);
    };

    /* Both tiers of a subscript's bounds checking, in the order they must run: the constant one decides
       without emitting, the runtime one emits only when the constant one declined. Callers get the
       out-of-range finding back to hang on the meta they build. */
    checkSubscript = function (extent, idxOp, sourceCode, sourceOffset) {
        emitRangeCheck(idxOp, extent, sourceCode, sourceOffset);
        var oob = checkConstIndex(extent, idxOp, sourceCode, sourceOffset);
        return (oob === undefined ? undefined : [ oob ]);         /* `oobIndex` is a LIST - see checkIndexUse */
    };

    /* A constant index that tier 1 could not clear survived the subscript as a flag, because only its USE
       decides whether it is an error. Anything that reads or writes the element lands here; `&` cleared
       it. A numeric extent is decided now; a symbolic one becomes a DEFERRED assertion, asked of the
       assembler in the canonical `! LSSi` / `! FAIL` / skip-label form (design/impala/TwoStageConstants.md rule 4,
       the same shape assertFitsExtent uses for an over-filled initializer). It costs no runtime
       instruction and is emitted only at a real dereference, so `&s.v[9]` stays legal here too. */
    /* A SHAPED subscript leaves one finding PER AXIS, each naming a different bound, so a subscript carries
       a LIST. Arbitrating down to one silently dropped every axis but the first: `cells[i, j]` on a shape
       with two host-supplied axes then guarded `i` and let `j` run off its row. Axis findings are pushed
       first, so an E461 that can name the axis is the one that fires - the flat check has nothing to say
       about `cells[0, 5]`, a legal word offset and an illegal coordinate. */
    checkIndexUse = function (expr) {
        var list = expr.oobIndex;
        if (list === undefined) return;
        expr.oobIndex = undefined;
        for (var i = 0; i < list.length; ++i) {
            indexGuard(list[i]);
        }
    };

    indexGuard = function (op) {
        if (op.n !== undefined) {
            fail('Index ' + op.k + ' is outside the extent ' + op.n + ' of this array',
                    op.src, op.off, 'E461',
                    'valid indices are 0 to ' + (op.n - 1)
                            + ' - taking its ADDRESS is legal, reading or writing it is not');
        }
        /* Emitted RIGHT HERE, immediately before the access it guards. It used to be queued and flushed at
           the end of the function, because `declare` writes straight to the output stream and flushes
           pending metacode first, which a mid-expression call cannot survive. `emit` has neither problem:
           it appends to metacode in order, exactly as the subscript's own `! ADDi` does. The only thing
           deferral ever bought was collapsing several indices into one array down to the largest, and
           that never fired once in the corpus - every duplicate was the SAME index accessed twice (read
           and written), which a per-function seen-set removes just as well.

           Emitting in place also costs one line less: the skip label rides on the guarded instruction
           instead of needing a bare `!` of its own. */
        /* An OWNED copy is never deduplicated and never scaled: it was taken from the folded value the
           subscript pushed into the offset, which is already in `.z.` units, and its name is a recycled
           scratch that says nothing about which index it holds - so it cannot key anything either. */
        var key = op.own ? undefined : op.ext.sym + '|' + op.k;
        if (key !== undefined) {
            if (emittedGuards[key]) {
                return;                                           /* the same assertion, already asked */
            }
            emittedGuards[key] = true;
        }
        var ok  = newLabel('g');
        var lhs = '#' + op.k;
        var w;
        if (op.ext.stride !== undefined && !op.own) {             /* `.z.` counts WORDS, the index counts
                                                                     ELEMENTS - scale before comparing */
            w = scaleByStride(op.k, op.ext.stride);
            lhs = '#' + w;                                        /* freed below - a no-op when it folded */
        }
        var low;
        if (typeof op.k !== 'number') {
            /* Anything but a literal may be negative, so it needs BOTH bounds (a literal was settled by
               E461). Both are plain comparisons: the low one falls through into the FAIL, whose label
               RIDES it - assemble-time branches resolve against a line that folds away. This used to be
               `(extent - 1 - k) | k >= 0`, three extra ALU ops bought purely to avoid a second label
               back when flushMetaCode spent every one on a NOOP. */
            low = newLabel('g');
            emit('<> <', 'i', lhs, '#0', low);
        }
        emit('<> <', 'i', lhs, '#' + op.ext.sym, ok);
        if (w !== undefined) { returnBack(w); }
        if (low !== undefined) {
            emit('<-?', true, low, undefined, undefined);
            metacode[metacode.length - 1].mayRide = true;   /* only the `! FAIL` below */
        }
        emit('<> FAIL', undefined,
                (op.own ? 'a computed index' : 'index ' + op.k)
                        + ' outside ' + op.ext.what + ' (resolved only at assembly)',
                undefined, undefined);
        emit('<-?', true, ok, undefined, undefined);
        metacode[metacode.length - 1].mayRide = true;   /* the guarded instruction, or the
                                                                             next guard's first comparison */
        if (op.own) {
            returnBack(op.k);                            /* held since the subscript, for exactly this */
        }
    };

    /* ONE deferred assertion, in the canonical `! <CMP>` / `! FAIL <text>` / skip-label form
       (design/impala/TwoStageConstants.md rule 4): the assembler decides what Impala could not, aborts with a real
       sentence, and the whole thing costs no runtime instruction. Every caller goes through here so the
       shape has one home - it had two before, drifting apart by a `declare` argument at a time. `.g<N>`
       uses guardCounter, NOT labelCounter, which resets per function. */
    assembleAssert = function (tests, message, sourceCode, sourceOffset) {
        var ok = '.g' + (guardCounter++);
        var bad = (tests.length > 1) ? '.g' + (guardCounter++) : ok;
        for (var i = 0; i < tests.length; ++i) {
            /* The LAST test jumps past the failure when it holds; every earlier one jumps TO it, so a
               caller with two bounds supplies the first already inverted. Two tests share one `! FAIL`
               and one message - the same two-tests-one-failure shape the runtime tier uses. */
            declare('! ' + tests[i][0], 'globals', undefined, 'i', true,
                    tests[i][1] + ' @' + (i + 1 < tests.length ? bad : ok), sourceCode, sourceOffset);
        }
        declare('! FAIL ' + message, 'globals', (bad === ok ? undefined : bad),
                undefined, true, undefined, sourceCode, sourceOffset);
        declare('!', 'globals', ok, undefined, true, undefined, sourceCode, sourceOffset);
    };

    makeRValue = function (expr, classes) {
        classes = classes || '#<&^$%';

        expr = metaSlot(expr);
        checkIndexUse(expr);                              /* reading it - reference() would have cleared the flag */

        if (expr.place && expr.arrayOf) {                         /* array place used without a subscript -> decay to a pointer */
            var delem = expr.arrayOf;
            var dt = placeAddress(expr);
            makeMeta(expr, ':=', 'p', undefined, dt, undefined);
            setElem(expr, delem);
            return dt;
        }

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

        /* An ARGUMENT is a reading context like any other, and a place is not an instruction: `g(f.state)`
           handed the writer a raw `@place` record, whose operator is in no opcode table, and the compiler
           died on `Cannot read properties of undefined` with no code, position or caret. Only this door
           was missing - `p = f.state` and `&f.state[0]` both decay - because the arg path skips
           makeRValue on purpose (it emits straight into the call window instead of a temp). Ask it about
           the place and nothing else: it returns the moment it has decayed one, and it owns what every
           other reader means by a place, so a new place shape cannot be right for readers and fatal here.
           checkIndexUse is idempotent (it clears oobIndex on entry), so Argument having already run it is
           not a double guard. */
        if (expr.place) {
            makeRValue(expr);
        }

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

        claimSlot(number);           /* remove %<number> from the free list if present */

        expr.operands[0] = tgt;
        emitMeta(expr);
    };

    /* --------------------------------------------------------- *
     *  Typed error helper                                       *
     * --------------------------------------------------------- */

    function verboseType(t) {                                  /// never leave a type blank in a diagnostic
        var v = VERBOSE_TYPES[t];
        return (v !== undefined ? v : (t !== undefined ? "'" + t + "'" : 'unknown'));
    }

    typeError = function (desc, source, offset, type1, type2, code, hint) {
        var message = replace(desc, '{$type1}', verboseType(type1));
        if (type2 !== undefined) {
            message = replace(message, '{$type2}', verboseType(type2));
        }
        fail(message, source, offset, code, hint);
    };

    /* --------------------------------------------------------- *
     *  Strict-expression helpers (Impala 2)                     *
     * --------------------------------------------------------- */

    /* `return`, `break` and `continue` are reserved, and none of them is in the KEYWORD alternation:
       `return` is a statement rule of its own, and the other two have no lowering at all. So nothing
       stopped any of them being used as a NAME, and the complaint then landed at the USE naming the
       wrong thing - `locals int return` was accepted, and `return = 5;` reported E448 "return does not
       take a value", while `break`/`continue` reached E403 "Undeclared identifier". One rule, called
       from every door that takes a name. Still a --legacy warning: in 1.x these WERE ordinary
       identifiers, so old code that used one has to keep building. */
    RESERVED_NAMES = { 'return': true, 'break': true, 'continue': true };
    /* ONE occurrence of a name, one diagnostic. Two doors legitimately ask about the same name - the
       declarator sees every variable INCLUDING locals, `claimTopName` sees every top-level name INCLUDING
       functions and structs - and only their overlap is wrong. Deduping where the diagnostic is RAISED
       keeps both doors honest; deleting either one would silently drop a whole class of name. (Visible
       only under `--legacy`, where this is a warning: `global int break` reported itself twice, once as a
       "variable" and once as a "global". Strict mode throws on the first.) */
    checkReservedName = function (name, what, sourceCode, sourceOffset) {
        if (RESERVED_NAMES[name] !== true || reservedAt === sourceOffset) {
            return;
        }
        reservedAt = sourceOffset;
        strictError("'" + name + "' is a reserved word, not a"
                        + ('aeiou'.indexOf(what.charAt(0)) < 0 ? ' ' : 'n ') + what + " name",
                sourceCode, sourceOffset, 'E449',
                'rename the ' + what + '; --legacy still accepts a reserved word here');
    };

    strictError = function (message, source, offset, code, hint) {   /// error by default; warning under `--legacy`
        if (!legacyMode) {
            fail(message, source, offset, code, hint);
        } else if (typeof _hostOptions !== 'undefined' && _hostOptions != null
                && typeof _hostOptions.warn === 'function') {
            _hostOptions.warn(message, offset, code, hint);
        }
    };

    /* The integer behind a folded operand, or undefined when it is symbolic (`<A>`, a host `! DEFi`, an
       `extern struct` size). EVERY compile-time check gates on this: Impala may only reject a value it
       genuinely knows, and not knowing is not the same as being fine - so a symbolic operand is passed
       over in silence rather than guessed at. See design/impala/CompileTimeHardening.md.

       It must decode every spelling `IntegerLiteral` accepts, or a literal Impala plainly knows reads as
       symbolic and every gate downstream stands down: `global int array g[0x4]` disarmed E461, and
       `[-0x1]` walked past E462 and shipped `! DEFi #-0x1`, which lays a struct out BACKWARDS. Decimal
       alone also missed the legal leading `+`. A CHARACTER literal is deliberately still unknown - only
       GAZL decides what `'ab'` is worth, and agreeing with it by guess is how the two stages drift. */
    constInt = function (operand) {
        var m = (typeof operand === 'string'
                ? /^#([-+]?)(?:0[xX]([0-9A-Fa-f]+)|([0-9]+))$/.exec(operand) : null);
        if (m === null) return undefined;
        var n = (m[2] !== undefined ? parseInt(m[2], 16) : parseInt(m[3], 10));
        return (m[1] === '-' ? -n : n);
    };

    /* `SWCH` resolves table entries 0..size-1 only, so a case outside that window is unreachable - and a
       NEGATIVE offset folds to `.sN.-6`, which the assembler rejects as an invalid identifier, failing a
       build the compiler accepted without a word. A repeated value mints the same `.sN#K` label twice and
       trips `Symbol already defined` on a name the user never wrote. They need DIFFERENT things: the
       window check wants a numeric range, the duplicate check wants only the values, so a symbolic
       range narrows this to the duplicate half rather than switching both off. */
    checkCaseValue = function (ctx, value, source, offset) {
        if (ctx === undefined || value === undefined) {
            return;
        }
        /* Key the duplicate check on the RAW value. It used to read `caseSeen[value - fromNum]`, which
           borrowed the range base it has no need of and put it behind the same early return as the
           window check - so a SYMBOLIC range (`switch (i == LO to HI)`, `LO` a named const, which
           `constInt` deliberately never folds) silently disabled it. The two arms then minted `.sN#K`
           twice and the build died at assembly on `Symbol already defined: .s0.0`, naming a
           compiler-minted label the user never wrote. A repeat is a repeat whatever the base is. */
        if (ctx.caseSeen[value] === true) {
            fail('Duplicate case value ' + value, source, offset, 'E443',
                    'each case value may appear once in a switch');
        }
        ctx.caseSeen[value] = true;
        if (ctx.fromNum === undefined) {
            return;              /* symbolic range: the window is genuinely unknowable here - see S5 */
        }
        /* Subtract here rather than reading the emitted offset: `subConstInt` defers to an assemble-time
           `! SUBi <A>` whenever `from` is non-zero, so the offset operand is symbolic in exactly the
           `switch (i == 5 to 9)` shape that produces the unloadable `.sN.-6`. */
        var off = value - ctx.fromNum;
        /* The two halves are INDEPENDENT, and tying them together was the same mistake S6 fixed for the
           duplicate check: this one borrowed `sizeNum` it has no need of. Below the start is decidable
           from `from` ALONE - `switch (op == 1 to LAST_OP)` with a `case 0` is out of the window for
           EVERY value `LAST_OP` can take - and it is the half that breaks the build, because a negative
           offset is pasted into the label text (`.s0.-1`). Above the end is the opposite: it needs
           `sizeNum`, and when the end is host-supplied it must stay legal, because a configuration may
           narrow the window and an unreachable arm is not an error (design/impala/TwoStageConstants.md).
           So: always test the low end when `from` is known, test the high end only when the size is. */
        var whole = (ctx.sizeNum !== undefined);           /* both ends known -> name the whole window */
        if (off < 0 || (whole && off >= ctx.sizeNum)) {
            fail('Case value ' + value + (whole
                            ? ' is outside the switch range ' + ctx.fromNum
                                    + ' to ' + (ctx.fromNum + ctx.sizeNum)
                            : ' is below the switch range, which starts at ' + ctx.fromNum),
                    source, offset, 'E444',
                    whole ? 'the upper bound is exclusive, so the last reachable case is '
                                    + (ctx.fromNum + ctx.sizeNum - 1)
                          : 'a case below the start can never be reached, whatever the end of the range is');
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
     *  A descriptor is a chain of one-letter KIND CODES read     *
     *  left to right, with no separator:                         *
     *      i f          a scalar, ends the chain                 *
     *      p<rest>      pointer to <rest>:  pi, ppi, pSFilter    *
     *      S<name>      struct, <name> ends the chain: SFilter   *
     *      t<name>      funcptr type, ends the chain:  tTickFn   *
     *  A tail after `p` is therefore always another DESCRIPTOR,  *
     *  and a tail after `S`/`t` is always a NAME. That is the    *
     *  whole rule, and it is what keeps a struct called `p` or a *
     *  functype called `i` from reading as a built-in code -     *
     *  a bare head is ALWAYS built-in.                           *
     * --------------------------------------------------------- */

    descHead = function (desc) {                         /// 'pi' -> 'p'; 'SFilter' -> 'S'
        return (desc === undefined ? undefined : desc.charAt(0));
    };

    descTail = function (desc) {                         /// 'pi' -> 'i'; 'i' -> undefined
        return (desc === undefined || desc.length < 2 ? undefined : desc.substr(1));
    };

    /* Split a descriptor into the (type, elem) pair the rest of the compiler carries. Whether `elem` is
       a descriptor or a name follows from `type`, per the rule above. */
    descTypeElem = function (desc) {                     /// 'pi' -> {p,i}; 'tTickFn' -> {t,TickFn}
        return { type: descHead(desc), elem: descTail(desc) };
    };

    structDesc = function (name) { return 'S' + name; };
    funcTypeDesc = function (name) { return 't' + name; };
    pointerDesc = function (desc) { return 'p' + (desc === undefined ? '' : desc); };

    /* A symbol table is a plain `{}`, so `table[name]` on an undeclared name reads an inherited
       Object.prototype member (`constructor`, `valueOf`, `toString`, `hasOwnProperty`, ...) - a function,
       not undefined. Route EVERY by-name table read through these two so a user identifier can never be
       mistaken for one. NuXJS stores those names as plain keys once written, so guarding the read is the
       whole fix under the target engine. */
    hasOwn = function (table, name) {                    /// own key only - never an inherited member
        return name !== undefined && table
                && Object.prototype.hasOwnProperty.call(table, name);
    };
    ownEntry = function (table, name) {                  /// table[name], but undefined for an inherited key
        return hasOwn(table, name) ? table[name] : undefined;
    };

    /* NAME predicates take a raw identifier and ask the tables; ATOM predicates take a DESCRIPTOR and
       read its kind code. Both are needed: the parser has a raw name in hand at `TypeBase`, everything
       downstream has a descriptor. Never cross them - `isStructAtom('Filter')` is false by design. */
    isStructName = function (name) {                     /// is the identifier `name` a defined struct?
        return hasOwn(structs, name);
    };

    isFuncTypeName = function (name) {                   /// is the identifier `name` a named funcptr type?
        return hasOwn(functypes, name);
    };

    isStructAtom = function (desc) {                     /// does descriptor `desc` denote a struct?
        return desc !== undefined && desc.charAt(0) === 'S' && desc.length > 1;
    };

    isFuncTypeAtom = function (desc) {                   /// does descriptor `desc` denote a funcptr type?
        return desc !== undefined && desc.charAt(0) === 't' && desc.length > 1;
    };

    /* The NAME inside an `S`/`t` descriptor. Takes a descriptor strictly - it is NOT identity on a bare
       name, because a struct genuinely called `Fn` would strip to `n`. */
    descName = function (desc) {
        assert(isStructAtom(desc) || isFuncTypeAtom(desc), 'descName needs an S/t descriptor');
        return desc.substr(1);
    };

    /* --------------------------------------------------------- *
     *  Named function-pointer types  (Impala 2 Step 3)          *
     * --------------------------------------------------------- */

    /* A functype emits NOTHING - no symbol, no layout, not even a `; signature` row - so unlike a
       struct definition (which owns its `.o.`/`.z.` constants) or a function (which owns a FUNC
       label) there is no artifact for a second declaration to collide with. Re-declaring one is
       therefore free PROVIDED the two agree, which is what lets a unit declare the functypes it uses
       and still be imported alongside another unit that declares the same ones. Set the earlier one
       aside and let endFuncType compare; emitting nothing also means a signature-row consumer never sees a
       functype, so this is the only place the disagreement can be caught at all. */
    beginFuncType = function (name, sourceCode, sourceOffset) {
        claimTopName(name, 'functype', sourceCode, sourceOffset);
        var shadowed = (isFuncTypeName(name) ? functypes[name] : undefined);
        functypes[name] = { shadowed: shadowed, params: [], returnList: [], returnCount: 0,
                returns: 'V', returnElem: undefined, returnStruct: undefined, returnWords: 0,
                complete: false, sourceCode: sourceCode, sourceOffset: sourceOffset,
                sourceName: sourceName };
    };

    /* By-value struct params/returns are PARKED for Impala 3.0 (design/ParkedFeatures.md). Every door that
       can introduce one rejects it HERE, so there is one owner instead of a copy per door - a `functype`
       declarator was previously unguarded, which let the whole parked by-value path run and, for an
       `extern struct`, baked a numeric COPY size against a host-owned layout. */
    rejectByValueStruct = function (type, struct, pname, isReturn, sourceCode, sourceOffset,
            atCall) {
        if (type !== 'S') return;
        if (isReturn) {
            fail('Returning a struct by value is not supported in Impala 2.0',
                    sourceCode, sourceOffset, 'E427',
                    'return it through a ' + struct + ' pointer out-parameter');
        }
        /* `atCall` is the ARGUMENT site, where the fix is at the call rather than in a signature. It is
           the door a declarator cannot guard: a name-only `extern function f` / `extern native f` has no
           parameter list to inspect, so the parked by-value path ran unopposed and baked a COPY size for
           a struct whose size Impala may not know - `*undefined` operands and a `*NaN` call window. */
        fail('Passing a struct by value is not supported in Impala 2.0',
                sourceCode, sourceOffset, 'E426',
                atCall ? 'pass its address instead: '
                                + (pname !== undefined ? '&' + pname : '&' + struct.charAt(0).toLowerCase())
                                + ', and take a ' + struct + ' pointer'
                       : 'take it by pointer: ' + struct + ' pointer '
                                + (pname !== undefined ? pname : struct.charAt(0).toLowerCase()));
    };

    /* A declared return value that is never written returns whatever the PREVIOUS call left in that frame
       slot - a silent wrong value, with nothing at any tier to catch it. Impala has no flow analysis and
       must not grow one here, so the test is deliberately the weakest one that cannot be wrong: the slot
       appears NOWHERE in the body, in any operand. A write in one arm of an `if`, a `for` counting with
       it, an `&r` handed to a callee - each makes it appear, and each stays silent. Only "never mentioned
       at all" is diagnosed, which is exactly the case a reader would call a typo. Runs before
       processBranches, which rewrites operands. */
    checkReturnAssigned = function (entry) {
        /* `pendingReturns` is cleared once the outputs are declared, which is before the body; the same
           row objects live on in the signature. */
        var list = (entry !== undefined && entry.signature !== undefined
                ? entry.signature.returnList : undefined);
        if (list === undefined) return;
        for (var i = 0; i < list.length; ++i) {
            var ret = list[i], seen = false;
            for (var j = 0; j < metacode.length && !seen; ++j) {
                var m = metacode[j];
                if (m.operator === ';') continue;         /* prose: the source line, not operands */
                for (var k = 0; k < m.operands.length; ++k) {
                    if (m.operands[k] === ret.name) { seen = true; break; }
                }
            }
            if (!seen) {
                fail('The return value ' + ret.rawName + ' is never assigned',
                        ret.sourceCode, ret.sourceOffset, 'E463',
                        'assign it somewhere in the body; an unassigned return value is whatever the '
                                + 'previous call left in that frame slot');
            }
        }
    };

    addFuncTypeParam = function (name, type, elem, struct, words, pname, sourceCode, sourceOffset) {
        rejectByValueStruct(type, struct, pname, false, sourceCode, sourceOffset);
        functypes[name].params.push({ type: type, elem: elem,
                struct: struct, words: words, name: pname });
    };

    addFuncTypeReturn = function (name, type, elem, struct, words, sourceCode, sourceOffset) {
        rejectByValueStruct(type, struct, name, true, sourceCode, sourceOffset);
        functypes[name].returnList.push({ type: type, elem: elem, struct: struct, words: words });
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
            ft.returns = 'V';
            ft.returnWords = 0;
        }
        var held = ft.shadowed;
        if (held) {
            var got = signatureShape(name, ft), had = signatureShape(name, held);
            if (got !== had) {                                    /* never a definition: a functype has only declarations */
                fail('functype ' + name + ' is already declared with a different shape: "'
                        + got + '" vs "' + had + '"', ft.sourceCode, ft.sourceOffset, 'E440',
                        'one functype cannot have two shapes - make the declarations identical, or keep only one');
            }
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

    elemVerbose = function (desc) {                      /// 'pi' -> "int pointer"; 'pSFilter' -> "Filter pointer"
        return renderDesc(desc, 'untyped', ' pointer',
                function (head) { return VERBOSE_TYPES[head]; });
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

    structDefined = function (name) {                    /// layout is known - the SIZE may still be symbolic
        var s = structs[name];
        return !!(s && s.complete);
    };

    /* Words occupied by one field, or undefined when that is not a number Impala knows - a symbolic
       extent (`int array v[N]`) or an element struct that has one. The EMITTED layout is symbolic
       either way (see emitStructLayout), so undefined means "the assembler knows this, we do not",
       never "incomplete" - structDefined is the test for that. This used to multiply the extent
       OPERAND ('N') by a number and hand back NaN, which flowed into s.words unchecked. */
    fieldWords = function (field) {                      /// words occupied by one field
        if (field.type === 'S') {
            return structWords(field.struct);
        }
        if (field.type === 'A') {
            var count = constInt('#' + field.size);
            var per = (isStructAtom(field.elem)
                    ? structWords(descName(field.elem)) : 1);
            return (count === undefined || per === undefined) ? undefined : count * per;
        }
        return 1;                                                 /* scalar i/f/p/F */
    };

    /* ONE namespace for every top-level name. Impala used to key each kind off its own table, while GAZL
       has a single flat symbol space - so `global int S` beside `function S()` passed every check and
       then failed to assemble ("Symbol already defined: S"), and `struct S` + `functype S` was rejected
       in one order and accepted in the other. Types are not emitted under a bare name, but they are read
       by position alone, and every symbol the compiler mints is keyed on a user name - so `.z.S` would
       be both `struct S`'s size and a global array `S`'s extent, and each new family would otherwise
       need its own collision analysis, two of which were only found by accident. Claiming names once
       retires the class, and is what lets one `.z.` tag serve every size (see extentSymbol).

       Re-claiming the SAME kind is how a declaration meets its definition, and how an import closure
       sees one unit twice; the per-kind agreement checks (E401 / E410 / E437 / E438 / E440) own that
       case, so only a DIFFERENT kind is a clash here. */
    var TOP_KINDS = { globals: 'global', functions: 'function', defines: 'const' };
    claimTopName = function (name, kind, sourceCode, sourceOffset) {
        if (kind === undefined || name === undefined || name === null
                || ('' + name).charAt(0) === '.') {
            return;                                           /* compiler-minted: no Impala identifier starts with `.` */
        }
        checkReservedName(name, kind, sourceCode, sourceOffset);
        var prev = ownEntry(topNames, name);
        if (prev !== undefined && prev !== kind) {
            fail('Name already used by a ' + prev + ': ' + name,
                    sourceCode, sourceOffset, 'E401',
                    'every top-level name must be unique - rename the ' + kind + ' or the ' + prev);
        }
        topNames[name] = kind;
    };

    beginStruct = function (name, sourceCode, sourceOffset) {
        claimTopName(name, 'struct', sourceCode, sourceOffset);
        var prev = ownEntry(structs, name);
        if (prev && prev.complete && !prev.extern) {
            fail('Struct already defined: ' + name, sourceCode, sourceOffset, 'E410');
        }
        /* A completed `extern struct` is a CLAIM about a layout, not a second definition of it, so
           set it aside and let endStruct compare the two rather than rejecting the definition it
           describes. The definition wins either way. */
        structs[name] = { shadowed: (prev && prev.complete ? prev : undefined),
                fields: [], words: 0, complete: false,
                sourceCode: sourceCode, sourceOffset: sourceOffset };
        openStruct = name;
    };

    /* An `extern struct` declaration and a `struct` definition of one name are two claims about one
       layout, and must agree - the E437 rule, for types. Whichever arrived second was parsed into a
       fresh entry so the first stayed intact; the DEFINITION is authoritative in either order.
       Returns true when the entry just parsed was the DECLARATION, i.e. it owns no layout to emit. */
    /* Do two claims about one struct describe the SAME layout? Compared field by field, because the
       field ORDER decides the offsets. `words` is derived and not compared.

       This used to string-compare two rendered `; signature` rows, which made the format of an
       INFORMATIONAL COMMENT decide which programs compile - and it was wrong in a reachable way.
       `extentText` renders an axis nobody can state as `[]`, on the documented understanding that a
       consumer "skips an empty one"; the consumer that did the skipping was gazl-validate, deleted
       2026-08-05, and the raw `!==` that inherited the job skips nothing. So `extern struct S { int
       array v[] }` never agreed with `struct S { int array v[3] }` - and E438's advice to correct the
       declaration ran straight into E430, "a host-owned array must not state a size". Two diagnostics,
       no legal spelling between them. */
    function statedAxis(v) {                                      /* a `<X>` fold is no more statable than undefined */
        return (v !== undefined && ('' + v).charAt(0) !== '<');
    }

    function axisOf(field, k) {                                   /* a 1-D array carries its extent in `size`, not `dims` */
        return (field.dims !== undefined ? field.dims[k] : (k === 0 ? field.size : undefined));
    }

    function sameLayout(a, b) {
        if (!a || !b || !a.fields || !b.fields) return true;      /* nothing to judge */
        if (a.fields.length !== b.fields.length) return false;
        for (var i = 0; i < a.fields.length; ++i) {
            var f = a.fields[i], g = b.fields[i];
            if (f.name !== g.name || f.type !== g.type) return false;
            if (f.elem !== g.elem || f.struct !== g.struct) return false;
            if (f.type !== 'A') continue;
            /* RANK is Impala's to check and both sides always state it; an EXTENT is compared only
               where both sides state one, which is what lets a host-owned `[]` meet a sized `[3]`. */
            var ra = (f.dims !== undefined ? f.dims.length : 1);
            var rb = (g.dims !== undefined ? g.dims.length : 1);
            if (ra !== rb) return false;
            for (var k = 0; k < ra; ++k) {
                var x = axisOf(f, k), y = axisOf(g, k);
                if (statedAxis(x) && statedAxis(y) && '' + x !== '' + y) return false;
            }
        }
        return true;
    }

    checkStructAgreement = function (name) {
        var parsed = structs[name], held = parsed.shadowed;
        if (!held) {
            return false;
        }
        if (!sameLayout(parsed, held)) {
            /* The rows are built for the MESSAGE only - the decision above never reads them. Rendering
               swaps the table entry, so put it back before failing rather than leaving `held` installed. */
            var parsedRow = structSignatureRow(name, false);
            structs[name] = held;
            var heldRow = structSignatureRow(name, false);
            structs[name] = parsed;
            /* fail() bakes its message, and bake() EVALS anything between braces (that is how
               `{$type1}` interpolation works), so a struct row's `{ a : int }` must not go in raw. */
            function shown(row) { return replace(replace(replace(row, 'signature ', ''), '{', '('), '}', ')'); }
            failDisagreement('struct ' + name, shown(parsed.extern ? parsedRow : heldRow),
                    shown(parsed.extern ? heldRow : parsedRow), 'E438',
                    !(parsed.extern && held.extern),              /* both extern -> no definition to arbitrate */
                    'struct', 'layouts', parsed.sourceCode, parsed.sourceOffset);
        }
        if (!parsed.extern) {
            /* Carry across any field references already made through the DECLARATION - the definition is
               about to emit the layout those references need to have seen. Without this the replacement
               below drops them and the use-before-define goes unreported (see emitStructLayout). */
            if (held.pendingRef !== undefined) parsed.pendingRef = held.pendingRef;
            structs[name] = parsed;                      /* a definition outranks the declaration it fulfils */
        } else {
            structs[name] = held;                        /* a declaration does not displace what is held.
                                                                     Stated outright: it used to fall out of the
                                                                     swap the row rendering happened to leave behind. */
        }
        return !!parsed.extern;
    };

    /* A field extent that needs folding lives in a `<X>` scratch that the layout block reads LATER, so
       the borrow must survive the declarator - otherwise a second such field folds into the same scratch
       and silently overwrites the first one's extent. ArrayDecl hands every extent to its consumer (see
       there); this is the struct's release, once the layout has read them. */
    endStruct = function (name) {
        var s = structs[name];
        s.complete = true;
        var redeclared = checkStructAgreement(name);
        if (!redeclared) {
            emitStructLayout(name);
        }
        returnExtent('struct ' + name, s.fields);
        openStruct = undefined;
        return redeclared;
    };

    /* Emit a struct's layout as GAZL compile-time constants: a rolling `<a>` accumulator that
       snapshots each field offset into `.o.Name.field` and the total size into `.z.Name`. Field
       access then references these symbols instead of baked numbers. Structs are defined in
       dependency order (a by-value field's struct must be complete first, E412), so `#.z.Inner`
       is always defined before an outer struct advances past it. Extern structs emit nothing
       (the host owns their layout). See design/impala/StructLayoutConstants.md. */
    emitStructLayout = function (name) {
        if (dry) return;
        if (typeof output !== 'function') return;
        var s = structs[name];
        if (!s || s.extern) return;                               /* extern: host provides all base offsets + .z */
        /* `! DEFi` constants resolve strictly top-down - unlike code labels, they get no forward-reference
           pass - so a `.o.` reference emitted ABOVE this block cannot see it. That is reachable in exactly
           one shape: a body-carrying `extern struct` in a unit the import closure emits EARLIER than the
           unit holding the real definition, which happens across a cycle, where no order satisfies both
           directions. It compiled clean and died at GAZL assembly with `Symbol not previously defined (in
           expected scope): .o.<name>.<field>`, naming a symbol the user never wrote. A genuinely
           host-owned `extern struct` never reaches here (returned above), so its references stay legal. */
        if (s.pendingRef !== undefined) {
            var _r = s.pendingRef;
            fail('The layout of struct ' + name + ' is used before it is defined',
                    _r.source, _r.offset, 'E464',
                    'this unit declares `extern struct ' + name + '` with a body and needs its layout '
                            + 'here - a value of that type, or one of its fields - but the real '
                            + 'definition is emitted later in the import closure; use the opaque form '
                            + '`extern struct ' + name + '` and reach it through a pointer, or break '
                            + 'the import cycle');
        }
        s.layoutEmitted = true;
        var T = (typeof TAB !== 'undefined') ? TAB : '\t';
        flushMetaCode('');                                   /* a field extent that folded to a `<X>` scratch was
                                                                         queued through emit(); drain it BEFORE the block so
                                                                         the definition precedes the ! ADDi that reads it
                                                                         (same rule declare() follows before its own output) */
        output(T + '! MOVi <a> #0' + T + '; layout of struct ' + name);
        for (var i = 0; i < s.fields.length; ++i) {
            var f = s.fields[i];
            output(fieldSymbol(name, f.name) + ':' + T + '! DEFi #<a>');
            if (f.type === 'S') {                                 /* nested by-value struct */
                output(T + '! ADDi <a> #<a> #' + extentSymbol(f.struct));
            } else if (f.type === 'A') {                          /* array: name the extent, THEN advance by it */
                var x = extentSymbol(f.name, name);
                var words = f.size;
                if (isStructAtom(f.elem)) {              /* count * element size, folded now - except at
                                                                    a count of one, where the size IS the answer */
                    var esym = extentSymbol(descName(f.elem));
                    if (constInt('#' + f.size) === 1) {
                        words = esym;
                    } else {
                        output(T + '! MULi <t> #' + f.size + ' #' + esym);
                        words = '<t>';
                    }
                }
                output(x + ':' + T + '! DEFi #' + words);
                /* A SHAPE also names each axis, so a subscript can stride by one and a bounds check can
                   test against one. `.z.` is the product and says nothing about how it is divided; these
                   are keyed on the STRUCT, so every value of it shares them - which is what lets an extent
                   survive a call boundary. Axis 0 is innermost, stride 1. */
                for (var d = 0; f.dims !== undefined && d < f.dims.length; ++d) {
                    output(axisSymbol(f.name, name, d) + ':' + T + '! DEFi #' + f.dims[d]);
                }
                if (constInt('#' + f.size) === undefined) {
                    /* A SYMBOLIC extent could still be negative - `const int K = -1`, or a host-supplied
                       `! DEFi`. Impala cannot see the number (ArrayDecl rejects the ones it can), and
                       nothing downstream would: this count is only ever ADDED to the offset accumulator,
                       so a negative one runs the layout BACKWARDS and lands the next field on top of an
                       earlier one, silently. Ask the assembler, in the same deferred form the index
                       checks use. Zero runtime cost, and only for a field whose extent is not a number. */
                    var neg = '.g' + (guardCounter++);
                    output(T + '! GEQi #' + x + ' #0 @' + neg);
                    output(T + '! FAIL extent of ' + name + '.' + f.name
                            + ' is negative, which would run the struct layout backwards');
                    output(neg + ':' + T + '! ADDi <a> #<a> #' + x);
                    continue;
                }
                output(T + '! ADDi <a> #<a> #' + x);
            } else {                                              /* scalar (int/float/ptr/funcptr) */
                output(T + '! ADDi <a> #<a> #1');
            }
        }
        output(extentSymbol(name) + ':' + T + '! DEFi #<a>');
    };

    /* `k * sym` as ONE assemble-time operand, with the k == 1 identity folded - a VALUE test, so `0x1`
       and `+1` fold exactly as `1` does. Returns an UNHASHED operand: the stride symbol itself when it
       folds, which costs no emitted line and no borrow, else a `<X>` the caller owns. `returnBack`
       no-ops on a plain symbol, so a caller frees the result unconditionally without caring which it
       got - that is what lets four callers share this at all.
       The other two sites that scale a count by a symbolic element size keep their own shape: the struct
       layout block writes through `output()` with its own hand-picked `<t>`, and mulAddAxis fuses the
       following add and chooses between a transient and a scratch. */
    scaleByStride = function (k, sym) {
        var n = '#' + k;                                      /* every caller passes it unhashed */
        if (constInt(n) === 1) { return sym; }
        var w = borrow('<');
        emit('<> *', 'i', w, n, '#' + sym);
        return w;
    };

    /* Fold a place's compile-time offset PARTS (field-offset symbols + constant strides) into one
       operand, emitting inline `! ADDi` (assemble-time, zero runtime cost) only when there are 2+
       parts. One part -> used bare; zero -> null (offset 0). The scratch is pool-managed (`<`),
       returned when the consuming meta is released, so simultaneously-live reads never collide. */
    foldOffset = function (parts) {
        if (!parts || parts.length === 0) return null;
        if (parts.length === 1) return parts[0];      /* a lone part (symbol or scratch) is returned as-is; its owner frees it */
        var o = borrow('<');
        emit('<> +', 'i', o, '#' + parts[0], '#' + parts[1]);
        for (var k = 2; k < parts.length; ++k) {
            emit('<> +', 'i', o, '#' + o, '#' + parts[k]);
        }
        for (var j = 0; j < parts.length; ++j) {      /* release borrowed stride scratches now folded into o */
            if (('' + parts[j]).charAt(0) === '<') returnBack(parts[j]);
        }
        return o;
    };

    /* `extern struct Name { fields }`: host-owned layout. Impala knows the interface (field
       names + types) and emits symbolic `.o.Name.field` / `.z.Name` references, but does NOT
       emit the layout - the host supplies those constants at load. See design/impala/StructLayoutConstants.md. */
    /* Never rejects: the body has not been parsed yet, so it is not yet known whether this
       declaration asserts anything at all. A BODYLESS `extern struct N` is the struct analogue of a
       name-only `extern function f` - an opaque handle making no layout claim - so it must not
       collide with a definition the closure already has; ExternDecl simply puts that definition
       back. A bodied one IS a claim, parsed alongside the definition and compared by endStruct. */
    beginExternStruct = function (name, sourceCode, sourceOffset) {
        claimTopName(name, 'struct', sourceCode, sourceOffset);
        var prev = ownEntry(structs, name);
        if (!prev || prev.complete) {
            structs[name] = { shadowed: (prev && prev.complete ? prev : undefined),
                    fields: [], words: 0, complete: false,
                    sourceCode: sourceCode, sourceOffset: sourceOffset };
        }
        structs[name].extern = true;
    };

    /* A BODYLESS `extern struct N` never reaches endStruct, so nothing compares it - and with no body
       it claims no layout, so there is nothing to compare. Give back whatever it was parsed beside. */
    cancelStructRedeclaration = function (name) {
        var s = structs[name];
        if (s && s.shadowed) {
            structs[name] = s.shadowed;
        }
    };

    isExternStruct = function (name) {
        var s = structs[name];
        return !!(s && s.extern);
    };

    /* The `*size` operand for a by-value struct ALLOCATION (LOCA / GLOB). An extern struct's real
       size is host-owned, so use the symbolic `.z.Name` (resolved at load); a normal struct uses its
       compiler-known word count. Frame/global allocation accepts a symbolic size, so by-value extern
       locals/globals work. (Passing/returning a struct BY VALUE is different - it reserves a
       compile-time-known number of transient VM slots, which a host-owned size cannot provide. By-value
       params/returns are parked wholesale today (E426/E427), so the extern-specific E425 that used to
       state this no longer fires; design/gazl/GAZLSymbolicWindows.md is where the reasoning lives.) */
    /* Note a use of a struct whose layout block has not been emitted yet. Legal on its own - a host-owned
       `extern struct` never emits one and the host defines the symbols - but if a REAL definition turns up
       later in the import closure, `emitStructLayout` reports these as use-before-define (E464). Callers
       without a source position simply do not record: every such site needs a local, global or field of
       the type, and those ARE recorded, so the struct is caught anyway. Only the FIRST use is kept -
       E464 names one position - and keeping only one also bounds the record for a host-owned `extern
       struct`, which never sets layoutEmitted and so would otherwise accumulate a note per use forever. */
    noteStructUse = function (structName, sourceCode, sourceOffset) {
        var st = structs[structName];
        if (st === undefined || st.layoutEmitted === true || sourceCode === undefined) return;
        if (st.pendingRef === undefined) st.pendingRef = { source: sourceCode, offset: sourceOffset };
    };

    structAllocSize = function (structName, sourceCode, sourceOffset) {
        noteStructUse(structName, sourceCode, sourceOffset);
        return '*' + extentSymbol(structName);                           /* always symbolic - adapts to the (possibly host/assembler-set) size */
    };

    /* The symbol naming an array's extent in WORDS - the `*size` operand, not the element count (a
       struct-element array is `count * .z.Elem`). Same tag as a struct's size because it is the same
       quantity: `.z.<path>` is the words occupied by `<path>`, whether that is `.z.Voice` (a struct),
       `.z.bank` (a global array), `.z.main.buf` (a local) or `.z.S.v` (a struct array field). One tag
       is only sound because a top-level name has exactly one kind (claimTopName) - otherwise `.z.S`
       would mean both `struct S` and a global array `S`. Struct array fields only: a scalar field is
       one word and a by-value field is `.z.Inner`, so those are already nameable.
       See design/gazl/SymbolNamespace.md. */
    extentSymbol = function (name, owner) {
        return '.z.' + (owner !== undefined ? owner + '.' : '') + name;
    };

    /* The symbol naming one FIELD's offset inside its struct - the third of the three naming schemes,
       alongside `.z.` (size) and `.d.` (axis). It was the only one built inline, in two places: the
       layout block that DEFINES it and the field access that REFERENCES it. One helper so a change to
       the scheme cannot reach one and miss the other. See design/gazl/SymbolNamespace.md. */
    fieldSymbol = function (structName, fieldName) {
        return '.o.' + structName + '.' + fieldName;
    };

    /* The symbol naming ONE AXIS of a shaped array, numbered by stride with axis 0 innermost. `.z.` is the
       total in words and cannot serve: there is one of it per array, so a rank-2 shape has nothing to
       stride by and nothing to compare pairwise. Same path scheme, so the same soundness argument applies -
       a top-level name has exactly one kind (claimTopName), and the axis suffix is a NUMBER where a field
       name would be an identifier, so `.d.S.v.0` can never collide with a field called `0`.
       See design/gazl/SymbolNamespace.md and docs/impala/MultidimensionalArrays.md. */
    axisSymbol = function (name, owner, axis) {
        return '.d.' + (owner !== undefined ? owner + '.' : '') + name + '.' + axis;
    };

    /* The record every bounds tier reads, built the same way for a global array, a local array and a
       struct ARRAY FIELD - only the owner path the symbols hang off differs, and a field adds two keys
       of its own at the call site. A SHAPE carries its axes as SYMBOLS, innermost first: a subscript
       strides by one and a bounds check tests against one, and both need a NAME rather than a value
       because the assembler - or the host - may be the one who knows it. Undefined for a 1-D array,
       which has no shape, which is what keeps its every path unchanged. */
    arrayExtent = function (name, owner, what, size, dims) {
        var syms;
        if (dims !== undefined) {
            /* A PLAIN LOOP, not `dims.map`: NuXJS has no `Array.prototype.map`, and this is the only
               path a shaped array takes - so every multidimensional program died there with
               "TypeError: map is not a function" while compiling identically under node. The compiler
               must run under BOTH engines (tools/run-nuxjs-impala-smoke.cmd), and the smoke test never
               reached this because none of its four programs declares a shape. */
            syms = [];
            for (var k = 0; k < dims.length; ++k) {
                syms.push(axisSymbol(name, owner, k));
            }
        }
        return { n: size, what: what, sym: extentSymbol(name, owner),
                 dims: syms,
                 dimN: dims };                                    /* the axis COUNTS as written, for checking */
    };

    /* Hand back every operand a declaration borrowed: the extent, and one per non-literal AXIS. THE
       CONSUMER OWNS THE BORROW (see ArrayDecl), so the release is here, at the three consumers that
       have finished reading them - endStruct after the layout block, FuncDecl after the locals pass,
       GlobalDecl right after its declaration. Returning only the total leaked one scratch per shaped
       array and tripped the pool's boundary assert before `main`.

       The whole BATCH goes back at once - every field of the struct, every declarator of the locals
       clause - because that is the scope the assert has to cover: two extents in one batch that folded
       into the SAME scratch means the borrow protocol broke upstream, and `returnBack` dedups silently,
       so nothing else would ever say so. */
    returnExtent = function (what, entries) {
        var held = {};
        for (var i = 0; i < entries.length; ++i) {
            var e = entries[i];
            var parts = (e.dims === undefined ? [ e.size ] : [ e.size ].concat(e.dims));
            for (var j = 0; j < parts.length; ++j) {
                var op = '' + parts[j];
                if (op.charAt(0) !== '<') continue;
                assert(!held[op], what + ' reuses extent scratch ' + op);
                held[op] = true;
                returnBack(op);
            }
        }
    };

    /* '*size' operand for allocating an ARRAY, published as a NAMED assemble-time constant rather
       than left in a `<X>` scratch. A scratch is a recycled register: it holds the extent only until
       the next borrow, so the allocation line was the one and only place that could read it. A named
       constant is permanent and referenceable, which is what lets an extent be quoted anywhere later.
       The value is usually symbolic - a host `! DEFi` count, or `count * .z.Elem` that only resolves
       at assembly - so naming it is the ONLY way to hold on to it; there is no number to keep. A
       struct-element array is that `count * .z.Elem`, folded by a compile-time `! MULi` so the
       allocation tracks the element size exactly like the subscript stride does; scalar elements are
       one word each, so the count IS the size. declare() flushes the fold ahead of the `! DEFi` that
       reads it, and hands the scratch back. */
    /* One extra axis of a shaped declaration. Same constant-folding and same negative check the first axis
       gets - an axis that runs backwards is exactly as wrong as an extent that does, and catching it here
       is what keeps the product honest. Returns the bare operand, as `$extent` holds it. */
    arrayAxis = function (expr, sourceCode, sourceOffset) {
        var v = makeConstant(expr, 'i', sourceCode, sourceOffset);
        if (constInt(v) < 0) {
            fail('Array axis is negative: ' + dropHash(v), sourceCode, sourceOffset,
                    'E462', 'every axis holds zero or more elements');
        }
        return dropHash(v);
    };

    /* The allocation size of a shape: the product of its axes, folded at ASSEMBLY time so a host-supplied
       axis works exactly as a literal one does. The result is a `<X>` scratch and follows the same rule the
       single extent does - THE CONSUMER OWNS THE BORROW, released by endStruct / FuncDecl / GlobalDecl. */
    axesProduct = function (dims) {
        var acc = dims[0];
        for (var i = 1; i < dims.length; ++i) {
            /* An axis of one contributes nothing to the product, and neither borrows nor emits for it. Safe
               against the ownership rule below because a skipped step creates no intermediate: `acc` can only
               BE one at i === 1, where it is axis 0's text rather than a borrow. */
            if (constInt('#' + dims[i]) === 1) { continue; }
            var next = scaleByStride(acc, dims[i]);
            if (i > 1 && ('' + acc).charAt(0) === '<') {
                returnBack(acc);                     /* an intermediate product, not an axis */
            }                                                 /* i === 1: acc IS axis 0, and `.d.<S>.<f>.0`
                                                                 has yet to quote it - endStruct frees it */
            acc = next;
        }
        return acc;
    };

    arrayAllocSize = function (elemDesc, count, name, owner, dims) {
        var symbol = extentSymbol(name, owner);
        var value = count;
        if (isStructAtom(elemDesc)) {
            value = scaleByStride(count,             /* one element: the size IS the allocation */
                    extentSymbol(descName(elemDesc)));
        }
        /* A DERIVED name must never be the one that reports a user's mistake. This runs as an ARGUMENT to
           the `declare` that registers the array itself, so JS evaluates it FIRST - and on a duplicate
           `global array g[2]` the `.z.g` collision fired before the `g` collision could, aborting with
           `Identifier already declared: .z.g` and no code, position or caret, naming a symbol the user
           never wrote. Skipping the redeclare lets the owner's own diagnostic (E401) arrive instead.
           Safe because `.z.g` derives from `g` alone: a collision here MEANS the owner collides too, and
           the stale definition dies with the compile a moment later. */
        if (symbols.defines === undefined || symbols.defines[symbol] === undefined) {
            declare('! DEF?', 'defines', symbol, 'i', true, '#' + value);
            /* A SHAPE publishes one constant per axis besides the total, exactly as a shaped struct FIELD
               does in its layout block: `.z.` counts WORDS, so it can neither stride nor bound a single
               coordinate. Inside the same duplicate guard - every one of these derives from the array's
               own name, so a collision here means the owner collides and E401 is about to say so. */
            for (var k = 0; dims !== undefined && k < dims.length; ++k) {
                declare('! DEF?', 'defines', axisSymbol(name, owner, k), 'i', true,
                        '#' + dims[k]);
            }
        }
        return '*' + symbol;
    };

    /* Is the array being declared HOST-OWNED - an `extern struct` FIELD, or a top-level `extern array`?
       Both say exactly the same thing about layout, which is nothing: the host supplies `.o.`, `.z.` and
       every `.d.`, and this side states rank alone. One predicate because they are one rule; they were
       two separate tests in two grammar branches, and only one of them had ever been given a rank. */
    hostOwnedArray = function () {
        return externArray
                || (openStruct !== undefined
                        && isExternStruct(openStruct));
    };

    /* The struct atom a pointer/array element descriptor points at, or undefined for anything whose
       stride is one word. `int pointer` and untyped `pointer` both land in the undefined case, which is
       why scaling changes nothing for them. */
    strideStruct = function (elemDesc) {
        return (isStructAtom(elemDesc) ? descName(elemDesc) : undefined);
    };

    /* One declarator, COPIED out of a VarDecl/ArrayDecl node for an ArgsDecl/LocalsDecl list. The copy
       is required, not tidiness: the declarator node is pooled and recycled by the parser, so anything
       holding a reference would later see whichever declarator came last. */
    /* Positional, not `declEntry($v)`: bare `$v` in an action is the rule's RETURN VALUE (`$v._`), while
       `$v.name` is the context field the declarator actually wrote - so passing the node whole silently
       hands over an empty object and every local is declared as `$undefined`. */
    declEntry = function (type, elem, struct, words, name, size, dims, at) {
        return { type: type, elem: elem, struct: struct, words: words, name: name, size: size,
                 dims: dims, at: at };
    };

    /* The six fields a SIGNATURE parameter keeps, copied out of a declarator list (`$inp`, counted by
       `.n`) or out of an already-copied prototype list (counted by `.length`) - hence the explicit
       count. Same reason declEntry copies: a declarator is a pooled parser node. `dims`/`at` are
       deliberately dropped, they are declaration-site detail a signature never reads. */
    copyParams = function (list, count) {
        var out = [];
        for (var i = 0; i < count; ++i) {
            var p = list[i];
            out.push({ type: p.type, elem: p.elem, name: p.name,
                       size: p.size, struct: p.struct, words: p.words });
        }
        return out;
    };

    addStructField = function (name, field, sourceCode, sourceOffset) {
        var s = structs[name];
        for (var i = 0; i < s.fields.length; ++i) {
            if (s.fields[i].name === field.name) {
                fail('Duplicate field ' + field.name + ' in struct ' + name,
                        sourceCode, sourceOffset, 'E411');
            }
        }
        if (field.type === 'S' && !structDefined(field.struct)) {   /* DEFINED, not numerically sized -
                                                                               a symbolic size nests fine (#.z.Inner) */
            fail('Field ' + field.name + ' has incomplete struct type ' + field.struct
                    + ' (define it, or use a pointer)', sourceCode, sourceOffset, 'E412');
        }
        field.words = fieldWords(field);
        s.fields.push(field);
        s.words = (s.words === undefined || field.words === undefined)
                ? undefined : s.words + field.words;              /* one symbolic field and the total is unknown */
    };

    /* Walk a struct layout against a brace-tree of constants, producing the flat list of
       constant operands (field order); trailing/omitted slots zero-fill. Recurses for
       nested struct and array fields. `items` is an array of { op, type } | { braced:[...] }. */
    /* One entry from a brace list, in a position where the INDEX does the naming. */
    indexedEntry = function (entry, sourceCode, sourceOffset) {
        if (entry !== undefined && entry.field !== undefined) {
            fail('`' + entry.field + ':` names a field, but an array element is positional',
                    sourceCode, entry.at, 'E458', 'drop the name - the position IS the index here');
        }
        return entry;
    };

    /* Map a struct's brace entries onto its fields BY NAME, or undefined for a positional list, which only
       --legacy still maps by position. Naming is the default because a positional list silently changes
       meaning the moment a field is inserted, removed or reordered - nothing in the source has to change
       for it to start initializing different fields.
          NOT a 1.x compatibility path, whatever `--legacy` suggests: Impala 1.0 has no structs at all
       (`docs/impala/Impala.md` hands `struct` to `docs/impala/Impala2.md` entirely), so no 1.x source can contain a
       struct initializer and there is nothing here to stay compatible WITH. What it tolerates is sources
       written against early 2.0 development, when positional was the only form. */
    fieldEntries = function (structName, fields, items, sourceCode, sourceOffset) {
        if (items.length === 0) {
            return undefined;                                 /* `{}` - every field zero-fills */
        }
        if (items[0].field === undefined) {
            strictError('Initializer for struct ' + structName + ' must name its fields',
                    sourceCode, items[0].at, 'E455',
                    'write `{ ' + fields[0].name + ': ... }` - a positional list changes meaning '
                            + 'whenever a field moves');
            /* Reached only under --legacy, where the positional list is mapped by index and anything
               past the last field is dropped. Named lists get E456 for a name no field has; the
               positional form has no name to report, so it needs the count rule instead. */
            if (items.length > fields.length) {
                failSurplus(structName, 'values', items.length, fields.length,
                        items[fields.length].at, sourceCode, sourceOffset);
            }
            return undefined;                                 /* --legacy: map by position */
        }
        var known = {}, byName = {}, i;
        for (i = 0; i < fields.length; ++i) {
            known['$' + fields[i].name] = true;               /* '$' prefix: a field may be named `constructor` */
        }
        for (i = 0; i < items.length; ++i) {
            var e = items[i];
            if (e.field === undefined) {
                fail('Initializer for struct ' + structName
                        + ' mixes named and positional entries', sourceCode, e.at, 'E455',
                        'name every field, or none of them');
            }
            if (known['$' + e.field] === undefined) {
                fail('struct ' + structName + ' has no field ' + e.field,
                        sourceCode, e.at, 'E456');
            }
            if (byName['$' + e.field] !== undefined) {
                fail('Field ' + e.field + ' is initialized twice',
                        sourceCode, e.at, 'E457');
            }
            byName['$' + e.field] = e;
        }
        return byName;
    };

    buildStructInit = function (structName, items, out, sourceCode, sourceOffset) {
        var fields = structs[structName].fields;
        /* The HOST owns an extern struct's field offsets and its size, so a positional DATA row is a
           guess at all three: field order, `.z.`, and whether there are fields Impala never saw. Reads
           already adapt (`POKE &g:.o.E.f`); only static data is early-bound, which is exactly why it is
           the only part that can be wrong. Emitting it anyway is the "half-deferred" shape
           design/impala/TwoStageConstants.md calls worse than either consistent choice. */
        if (isExternStruct(structName)) {
            blockInitFrom(out,
                    'the host owns the layout of struct ' + structName
                            + ', so Impala does not know which word it would land in', 'E459',
                    'a host-owned struct is initialized by the host - leave it zero-filled here; '
                            + 'static initialization of a host-owned layout needs GAZL 2 and is planned '
                            + 'for Impala 3.0 (design/ParkedFeatures.md)');
        }
        var byName = fieldEntries(structName, fields, items, sourceCode, sourceOffset);
        for (var fi = 0; fi < fields.length; ++fi) {
            var f = fields[fi];
            var item = (byName !== undefined ? byName['$' + f.name]
                    : (items && fi < items.length) ? items[fi] : undefined);
            if (f.type === 'S') {
                buildStructInit(f.struct, (item && item.braced) || [], out, sourceCode, sourceOffset);
            } else if (f.type === 'A') {
                /* Every array slot - 1-D or shaped, struct- or scalar-element, literal or symbolic extent -
                   goes through the one filler. The only thing a FIELD adds is that something may follow it
                   in the same allocation, so a gap of unknown size makes those later words unplaceable. */
                if (fillArray(f, structName, (item && item.braced) || [], out,
                        sourceCode, sourceOffset)
                        && out.blocked === undefined) {
                    blockInitFrom(out,
                            'the extent of ' + f.name + ' is not resolved until GAZL assembly time, so '
                                    + 'Impala cannot tell which word this would land in',
                            'E454',
                            'leave every field after ' + f.name + ' zero here - ' + f.name + ' itself may '
                                    + 'be initialized, and the assembler checks that the values fit; '
                                    + 'placing a field BEHIND a symbolic extent needs GAZL 2 and is '
                                    + 'planned for Impala 3.0');
                }
            } else {
                pushInitScalar(out, item, f.type, f.elem, f.name, sourceCode, sourceOffset);
            }
        }
    };

    /* Is every axis of a shape a literal Impala can count HERE? A symbolic axis (`sym[H, W * 2]`) has no
       compile-time length, so its nested init is checked at GAZL assembly instead - this picks which. */
    axesAllLiteral = function (dims) {
        for (var i = 0; i < dims.length; ++i) {
            if (constInt('#' + dims[i]) === undefined) {
                return false;
            }
        }
        return true;
    };

    /* Where a diagnostic about one initializer entry points: the entry's own position when it has one, else
       the initializer start. One helper so a caret rule cannot be right in one shaped-init message and wrong
       in the next. */
    entryAt = function (entry, sourceOffset) {
        return (entry !== undefined && entry.at !== undefined ? entry.at : sourceOffset);
    };

    /* The entry a COUNT complaint points at: the first surplus one when too many were given, else the last
       one actually given - a short group's mistake is at its end, not at the declaration's `=`. */
    countAt = function (group, want, sourceOffset) {
        return entryAt((group.length > want ? group[want] : group[group.length - 1]), sourceOffset);
    };

    /* Emit ONE array element into the buffer: a struct element recurses into buildStructInit, a scalar runs
       the full assignment check via pushInitScalar. The single leaf every array fill shares - the flat struct
       field loop, the shaped walk, and the struct-element global - so a rule added here reaches all three. */
    fillEntry = function (out, ev, structEl, elemDesc, name, sourceCode, sourceOffset) {
        if (structEl) {
            buildStructInit(descName(elemDesc), (ev && ev.braced) || [], out, sourceCode, sourceOffset);
        } else {
            var et = descTypeElem(elemDesc);
            pushInitScalar(out, ev, et.type, et.elem, name, sourceCode, sourceOffset);
        }
    };

    /* THE array filler. One slot record (`{ name, elem, size, dims }` - a struct FIELD entry, or a global
       array's declarator, which already share those keys) covers every case that used to be its own loop:
       1-D or shaped, struct- or scalar-element, field or standalone, literal or symbolic extent. A 1-D array
       is simply the rank-1 shape `[size]`, so there is no separate 1-D path to drift.

       Axes are innermost-first, as `dims` stores them; nesting level 0 is the OUTERMOST group, i.e. axis
       `rank-1-level`. This walker owns that conversion - callers pass `dims` untouched.

       Extent is a CHECK, never a fill bound. A literal axis is compared here; a symbolic one is handed to
       GAZL assembly, and the two checks it needs are different because the layout consequences are:
         - the OUTERMOST axis may stop short. That is a prefix, and the region zero-fills the rest, so only
           `words <= .z.name` has to hold (assertFitsExtent, emitted once over the whole array).
         - an INTERIOR axis must be exactly full, because a short group there leaves a gap of unknown size
           and positional `DATA` cannot skip one. Impala cannot know "full", so it requires every group to
           match the leftmost (rectangular) and asserts that length against `.d.name.k` with `! EQUi`.
       At rank 1 there are no interior axes, so the rule degenerates to "fill the prefix given, assert it
       fits" with nothing special written down for it.

       Returns true when the fill left a gap of unknown size, which is the caller's business: a struct FIELD
       has words after it that then become unplaceable (E454), a standalone array has nothing after it. */
    fillArray = function (slot, owner, tree, out, sourceCode, sourceOffset) {
        var axes = (slot.dims !== undefined ? slot.dims : [ slot.size ]);
        var rank = axes.length, from = out.length, k;
        var expected;                                         /* per-level counts; set only when an axis is symbolic */
        if (!axesAllLiteral(axes)) {
            expected = [];                                    /* leftmost spine gives one count per nesting level */
            var spine = [], node = tree;                      /* the groups themselves, so a count error can name one */
            for (k = 0; k < rank; ++k) {
                spine.push(node);
                expected.push(node.length);
                node = (node[0] && node[0].braced) || [];
            }
            for (k = 1; k < rank; ++k) {                      /* INTERIOR axes only - level 0 may be a prefix */
                var lit = constInt('#' + axes[rank - 1 - k]);
                if (lit !== undefined) {
                    if (expected[k] !== lit) {
                        fail('A shaped array with a symbolic axis must be filled exactly: axis '
                                + (rank - k) + ' holds ' + lit + ', but ' + expected[k] + ' given',
                                sourceCode, countAt(spine[k], lit, sourceOffset), 'E460',
                                'a partial fill cannot skip a symbolic stride - give every element');
                    }
                } else {
                    assembleAssert([['EQU?', '#' + expected[k] + ' #'
                            + axisSymbol(slot.name, owner, rank - 1 - k)]],
                            'initializer for ' + slot.name + ' has the wrong length on axis ' + (rank - k)
                                    + ': ' + expected[k] + ' given', sourceCode, sourceOffset);
                }
            }
        }
        fillAxis(slot, axes, 0, expected, tree, out, sourceCode, sourceOffset);
        if (expected !== undefined) {                         /* the one bound a symbolic extent still needs */
            assertFitsExtent(out.length - from, slot.name, owner, sourceCode, sourceOffset);
        }
        return (expected !== undefined);
    };

    fillAxis = function (slot, axes, level, expected, tree, out, sourceCode, sourceOffset) {
        var rank = axes.length;
        var inner = (level + 1 >= rank);
        var structEl = isStructAtom(slot.elem);
        var count;
        if (!inner && tree.length > 0) {
            /* a scalar where a brace group belongs - a flat list given for a shape. Reported before the
               count checks so `{ 1, 2, 3, 4, 5, 6 }` for a `[2, 3]` reads as "needs nesting", not "too many".
               indexedEntry FIRST, so a `field:` name here still gets its own E458 rather than this message. */
            var first = indexedEntry(tree[0], sourceCode, sourceOffset);
            if (first !== undefined && first.braced === undefined) {
                fail('Not enough braces in initializer for ' + slot.name
                        + ': a shaped array nests one group per axis', sourceCode,
                        entryAt(first, sourceOffset), 'E422');
            }
        }
        var axisLen = constInt('#' + axes[rank - 1 - level]);
        if (axisLen !== undefined) {
            if (tree.length > axisLen) {
                failSurplus(slot.name, (inner ? 'elements' : 'groups'), tree.length, axisLen,
                        tree[axisLen] && tree[axisLen].at, sourceCode, sourceOffset);
            }
            /* Pad to the axis: a short group in the MIDDLE must still occupy its slots or everything after
               it shifts. A short group at the END costs nothing either - emitInitData drops a trailing run
               of zero words, so "fill only what is needed" is decided once, there, not in this loop. */
            count = axisLen;
        } else {
            count = tree.length;                              /* unknown length - place the prefix given */
            if (level > 0 && tree.length !== expected[level]) {
                fail('A shaped array with a symbolic axis must be rectangular: axis '
                        + (rank - level) + ' holds ' + expected[level] + ', but a group gives ' + tree.length,
                        sourceCode, countAt(tree, expected[level], sourceOffset), 'E460',
                        'give every group the same length');
            }
        }
        for (var e = 0; e < count; ++e) {
            var ev = (e < tree.length) ? indexedEntry(tree[e], sourceCode, sourceOffset) : undefined;
            if (!inner) {
                if (ev !== undefined && ev.braced === undefined) {
                    fail('Not enough braces in initializer for ' + slot.name
                            + ': a shaped array nests one group per axis', sourceCode,
                            entryAt(ev, sourceOffset), 'E422');
                }
                fillAxis(slot, axes, level + 1, expected, (ev && ev.braced) || [], out,
                        sourceCode, sourceOffset);
            } else {
                fillEntry(out, ev, structEl, slot.elem, slot.name, sourceCode, sourceOffset);
            }
        }
    };

    pushInitScalar = function (out, item, type, elem, fieldName, sourceCode, sourceOffset) {
        if (item === undefined) {
            out.push(ZEROES[type]);                      /* omitted -> zero, and INVENTED: `out.given`
                                                                     does not advance, so emitInitData may drop
                                                                     it if nothing explicit follows */
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
        /* THE FIFTH DOOR. The other four initializer doors run the full assignment check; a struct FIELD
           reached the row through here and was only asked its coarse type, so `struct P { int pointer p }`
           took `&global floatArray[0]` and a funcptr field took a mismatched function, both silently -
           while the in-function assignment to the same field is E201/E441. A braced entry is already
           reduced to its operand, so rebuild the one-operand meta shape those checks read: `&NULL` and a
           direct `&f` must both stay recognisable. */
        checkInitTarget(type, elem,
                { type: item.type, elem: item.elem, operands: [ undefined, item.op ] },
                sourceCode, (item.at !== undefined ? item.at : sourceOffset));
        /* This is the ONLY place a word enters the row, so it is where "no word past here is placeable"
           belongs: here the entry still knows its own field name and source position, whereas by
           emitInitData it is a bare operand string in a flat array. Zero is exempt - the region fills
           with it under any layout, so those words are simply dropped. */
        var b = out.blocked;
        if (b !== undefined && !isZeroWord(item.op)) {
            fail('Cannot initialize ' + fieldName + ': ' + b.why,
                    sourceCode, (item.at !== undefined ? item.at : sourceOffset), b.code, b.hint);
        }
        out.push(item.op);
        out.given = out.length;                               /* the source WROTE this word - an explicit `0`
                                                                 is kept, unlike the padding around it */
    };

    /* Mark every word from here on UNPLACEABLE: Impala cannot say which GAZL word it would land in.
       `DATA` is positional and GAZL 1 cannot skip a symbolic number of words - there is no fill
       directive, and a backward `! GOTO` does not assemble (`Compile time label not found`), so no
       assemble-time loop can emit them either. Such words are only harmless when they are ZERO, which is
       what the region fills them with anyway, so emitInitData requires that and then drops them. Two
       situations reach this and they share everything but the message: a symbolically-sized array field
       (E454), and a host-owned `extern struct` layout (E459). */
    /* Surplus initializer entries are dropped by every fill loop (they stop at the extent), so they
       vanish with nothing wrong for the assembler to see. Both loops - a struct's array FIELD and a
       global array OF structs - need the identical rule, so it lives here once. `at` is the surplus
       entry's own position, so the caret lands on it rather than on the initializer's `{`. */
    failSurplus = function (name, what, given, holds, at, sourceCode, sourceOffset) {
        fail('Too many initializer ' + what + ' for ' + name + ': ' + given
                + ' given, but it holds ' + holds,
                sourceCode, (at !== undefined ? at : sourceOffset), 'E460',
                'remove the extra ' + (given - holds) + ', or widen ' + name);
    };

    /* Hand a comparison Impala cannot make to the stage that can: `words <= .z.Struct.field`, deferred
       to GAZL assembly time, where the extent finally has a value. Rule 4 of design/impala/TwoStageConstants.md,
       and it costs nothing at run time - every line is an `!` directive, so no word is emitted either
       way.

       `! LEQi` + `! FAIL` + a skip label is the CANONICAL idiom - design/impala/TwoStageConstants.md prescribes
       it and src/UnitTest.gazl:30-40 guards GAZL_VERSION exactly this way. An earlier version of this
       branched to a deliberately UNDEFINED label so the label name doubled as the message; that is the
       trick TwoStageConstants.md and CompileTimeHardening.md both explicitly ban, because an identifier
       cannot carry spaces - so it could not name the two counts that make the message actionable.

       It must sit ABOVE the rows it guards. GAZL checks only the WHOLE allocation (`Not enough space in
       data section: s`), so an over-filled FIELD that still fits the struct total spills into the next
       field and assembles silently; and where the total DOES overflow, whichever check comes first in
       the file wins, so emitting below the rows would trade this message for the coarser one. Verified
       against GAZLCmd on 2026-08-02: words == extent passes, both over-fill shapes fail here. */
    /* `owner` is the struct a FIELD belongs to, or undefined for a standalone array - the same two spellings
       `extentSymbol` and `axisSymbol` already take, so one rule serves a field and a global alike. */
    assertFitsExtent = function (words, name, owner, sourceCode, sourceOffset) {
        if (words <= 0) {
            return;                                           /* nothing given - trivially fits */
        }
        var extent = extentSymbol(name, owner);
        assembleAssert([['LEQ?', '#' + words + ' #' + extent]],
                'too many initializer values for ' + (owner !== undefined ? owner + '.' : '') + name + ': '
                        + words + ' given, room for ' + extent, sourceCode, sourceOffset);
    };

    /* The mnemonic for one flat-array initializer row. `DATi`/`DATf`/`DATp` apply their type to EVERY
       operand on the line (`src/GAZL.cpp:996-1019` - one loop shared by all four, `accepts` picked from
       the 4th mnemonic character), so the assembler re-checks what Impala already checked. That second
       line of defence is the point: while these rows were `DATA` - which takes `KONST`, anything at all -
       Impala was the ONLY check, which is how a tautological one went unnoticed. It also covers what
       Impala cannot fold: `DATi #N` verifies that `N` really is `! DEFi` and not `! DEFf`.

       `DATA` stays for an UNTYPED array, which states no element type to check against. Struct
       initializers keep it too, and must: one row spans fields of different types (`DATA #1 #2.5 &s`),
       which is exactly the mixed case `DATA` exists for - see `consts.mixed` in src/UnitTest.gazl. */
    /* The COARSE type an entry answers to. `makeConstant` knows the three scalar heads and nothing else -
       a funcptr element head is a TYPE NAME it would not recognise - so the funcptr and pointer-element
       questions belong to `checkInitTarget`, which reads the same descriptor whole. Derived, not stored:
       one `initTarget` cannot disagree with itself, where three parallel slots could. */
    coarseType = function (desc) {
        if (desc === undefined) {
            return undefined;                                 /* untyped: a struct's row spans mixed field types */
        }
        var t = descTypeElem(desc).type;
        return (t === 'i' || t === 'f' || t === 'p' ? t : undefined);
    };

    initCoarseType = function () {
        return coarseType(initTarget);
    };

    initElemRow = function () {
        return (initCoarseType() !== undefined ? 'DAT?' : 'DATA');
    };

    /* How many compile-time scratches could still be handed out: `counters` is how many names have ever been
       minted, `stock` the ones handed back, and `borrow` mints at most 26. */
    freeScratches = function () {
        return (26 - counters['<']) + stock['<'].length;
    };

    /* Make a computed initializer value DURABLE so its scratch can go back to the pool.

       A value only the assembler can fold - `1 << P` on a named const - arrives as `<A>`, one of 26, and a
       brace initializer holds every entry's value until the whole group is placed (it must: named fields may
       be written out of order). So a list of 27 folded constants ran the pool dry and died with "expression
       too complex" - which is what capped EVERY nested initializer at 26 computed entries while the flat,
       streaming one had no limit.

       Capturing into a named define is what this compiler already does for `.z.`/`.d.` (`! MULi <A> ...`
       then `.z.g: ! DEFi #<A>`), and it is safe for exactly the reason it first looks unsafe: the emitted
       line that names `<A>` is its DEFINITION, which is what gives the capture its value, and the only
       PENDING reference is the operand string in the entry - still in memory, and it is what this returns.
       `declare` hands the scratch back itself, since returnBack reads the trailing `<X>` of `#<A>`.

       Done only once the pool is EMPTY, so an initializer that fits emits exactly what it always did. */
    holdConstant = function (op, type, sourceCode, sourceOffset) {
        if (op.charAt(0) !== '<' || freeScratches() > 0) {
            return op;
        }
        var name = '.k' + (holdCounter++);
        declare('! DEF?', 'defines', name, (type === 'f' ? 'f' : 'i'), true, '#' + op,
                sourceCode, sourceOffset);
        return '#' + name;
    };

    /* One entry of a flat initializer list: the full assignment check, then the rendered constant. Both
       list doors call this, so a rule added here cannot be live at the first entry and missing at the rest. */
    initEntry = function (x, sourceCode, sourceOffset) {
        var t = descTypeElem(initTarget);
        checkInitTarget(t.type, t.elem, x, sourceCode, sourceOffset);
        var coarse = initCoarseType();
        if (coarse === undefined) {
            coarse = metaSlot(x).type;                            /* untyped array: accept what comes */
        }
        return { op: holdConstant(makeConstant(x, coarse, sourceCode, sourceOffset),
                coarse, sourceCode, sourceOffset), type: coarse };
    };

    blockInitFrom = function (out, why, code, hint) {
        if (out.blocked === undefined) {                      /* the FIRST block bounds the row */
            out.blocked = { at: out.length, why: why, code: code, hint: hint };
        }
    };

    /* Zero is the one word that is safe to drop, because the region zero-fills to it under any layout -
       so test the VALUE, not the spelling. `0x0`, `-0`, `0.0e0` and `&NULL` are all zero; comparing
       against the canonical ZEROES strings rejected safe code that merely wrote one of them differently.
       A symbol (`#N`) is NOT zero as far as Impala is concerned - it does not know the value, and per
       design/impala/TwoStageConstants.md it must not guess one. */
    isZeroWord = function (operand) {
        if (typeof operand !== 'string') {
            return false;
        }
        if (operand === '&NULL') {
            return true;                                      /* null pointer / nullfunc */
        }
        var v = (operand.charAt(0) === '#' ? operand.substr(1) : '');
        return v !== '' && Number(v) === 0;                   /* NaN for a symbol -> not zero */
    };

    /* emit a flat constant list as one or more data rows (mirrors InitList chunking). The element type
       rides on the BUFFER (`ops.target`), set by whoever built it, NOT on the parser-lifetime `initTarget`:
       a scalar-element array states its element type, so its rows come out typed (`DATi`/`DATf`/`DATp`) and
       the assembler re-checks each word; a struct states no target, so it stays the untyped `DATA` one
       mixed-type row needs. Reading `initTarget` here instead would inherit a stale value from a previous
       declaration - an all-int array before a `{ int, float }` struct emitted `DATi #2.5`, which the
       assembler rejects. */
    emitInitData = function (ops, sourceCode, sourceOffset) {
        if (ops.blocked !== undefined) {
            ops.length = ops.blocked.at;                      /* the region zero-fills the remainder;
                                                                 pushInitScalar already refused any
                                                                 non-zero word past this point */
        }
        /* THE short-fill rule, in the one place every initializer passes through: drop the words the SOURCE
           NEVER WROTE. `out.given` is the high-water mark pushInitScalar sets for an explicit entry, so the
           padding invented for omitted elements and fields falls off the end while an explicit `0` stays -
           writing `{ a: 1, b: 0, c: 0 }` still emits three words, because you asked for three. Only the
           invention is dropped, and only the region's own zeros replace it (verified on GAZLCmd for GLOB,
           CNST and TEMP alike), which is what turned a `[100]` struct array given 14 entries from 400 words
           back into 56. Deciding it here rather than in each fill loop is what lets those loops pad honestly
           to the extent, which is what keeps a SHORT INTERIOR group from shifting everything after it. */
        if (ops.given === undefined) {
            ops.length = 0;                                   /* nothing explicit at all - the region is already this */
        } else if (ops.given < ops.length) {
            ops.length = ops.given;
        }
        var coarse = coarseType(ops.target);
        var row = (coarse !== undefined ? 'DAT?' : 'DATA'), type = (coarse || 'i');
        /* A `<X>` SCRATCH OPERAND GETS ITS OWN ROW, and that is a correctness rule, not formatting.
           `declare` ends by handing its emitted value back to the pool (`returnBack(value)`), and returnBack
           only recognises an operand it can parse whole - a scratch buried in `#8 <A> #7` is silently never
           returned, and the run dies on the boundary assert "compile-time scratch leak before main".
           `readonly S s = { x: 1 << P }` with P a named const did exactly that and never compiled. The flat
           `InitList` has always had this rule; the buffered path was written without it. */
        var line = '';
        for (var i = 0; i < ops.length; ++i) {
            if (line !== '' && (ops[i].charAt(0) === '<' || line.charAt(0) === '<'
                    || (line + ' ' + ops[i]).length >= 55)) {
                declare(row, 'globals', undefined, type, true, line, sourceCode, sourceOffset);
                line = '';
            }
            line += (line === '' ? '' : ' ') + ops[i];
        }
        if (line !== '') {
            declare(row, 'globals', undefined, type, true, line, sourceCode, sourceOffset);
        }
    };

    /* record one `returns` declarator on a function entry (Step 4: multiple return values,
       slice 2.5: by-value struct returns) */
    addReturn = function (entry, rawName, type, elem, size, sourceCode, sourceOffset, struct, words) {
        if (!entry.pendingReturns) {
            entry.pendingReturns = [];
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

    /* A place names a struct (or struct array) at base + a sum of compile-time offset parts. Only a
       terminal scalar field emits code, so struct values never enter the word temporaries. */
    setPlace = function (rec, baseKind, base, offParts, structName, arrayOf, dynIndex) {
        var slot = metaSlot(rec);
        slot.operator = '@place';
        slot.operands = [ undefined, undefined, undefined ];
        slot.place    = true;
        slot.baseKind = baseKind;                                 /* 'local' | 'pointer' | 'globalAddr' */
        slot.base     = base;
        slot.offParts = offParts || [];
        slot.arrayOf  = arrayOf;                                  /* set -> an array of `arrayOf`; a later [k] indexes it */
        slot.struct   = arrayOf ? undefined : structName;
        slot.type     = arrayOf ? 'p' : 'S';
        slot.elem     = arrayOf || (structName !== undefined ? structDesc(structName) : undefined);
        slot.dynIndex = dynIndex;                                 /* frame place + one runtime word-index -> terminal emits GETL/SETL */
        slot.extent   = undefined;                                /* pooled slot: never inherit another array's extent or
                                                                     another subscript's finding. The two callers that DO
                                                                     have an extent assign it after the call. */
        slot.oobIndex = undefined;
    };

    /* A place resolved to a terminal SCALAR location (a scalar field, or a scalar array element) becomes a
       value meta: local -> frame-relative MOV (GETL/SETL when a runtime index is present), global -> MOV*,
       pointer -> PEEK/POKE base <index-or-#offset>. `parts` are the compile-time offset parts to fold. */
    emitPlaceValue = function (x, bk, base, parts, dynIndex, type, elemTail) {
        var offOp = foldOffset(parts);
        /* Writability is carried by the OPERATOR here, exactly as it is for a scalar global (`:=*` has no
           assign() branch). A field of a `readonly` struct used to reach this as a plain `=*`, so the
           POKE was emitted and only the CNST region caught it, at load. */
        var ro = (x.readonly === true);
        if (bk === 'local' && dynIndex !== undefined) {           /* (dsp + base:off)[dynIndex] */
            makeMeta(x, '=[]$', type, null, base + (offOp ? ':' + offOp : ''), dynIndex);
        } else if (bk === 'local') {
            makeMeta(x, '=', type, undefined, base + ':' + offOp, undefined);
        } else if (bk === 'globalAddr') {                         /* &name:off in global memory */
            makeMeta(x, (ro ? ':=*' : '=*'), type, undefined, base + ':' + offOp, undefined);
        } else {                                                  /* pointer base: PEEK/POKE base <runtime index | #offset> */
            makeMeta(x, '=[]', type, null, base, dynIndex !== undefined ? dynIndex : '#' + offOp);
        }
        x.place = false;
        x.type  = type;
        x.elem  = elemTail;
        x.readonly = ro;                                          /* survives the makeMeta above, which
                                                                     clears it - assign() reads the FLAG */
    };

    /* structPtr[k] or arrayField[k] -> the element place. A constant index adds a symbolic stride to the
       offset parts (folds into the following .field, no runtime cost); a dynamic index materializes. */
    /* A bare operand dressed as a meta, so a folded index can be handed to `subscriptStruct` /
       `binaryOp` exactly where an Expr node goes. `makeRValue` reads `operands[1]` for a `:=` and hands
       it straight back, which is the whole trick. */
    wrapOperand = function (op, type) {
        return { operator: ':=', type: (type || 'i'), operands: [ undefined, op, undefined ] };
    };

    /* One Horner step: `acc * stride + idx`. Assemble-time throughout when every part is - a constant
       subscript then costs nothing at run time, exactly as a constant 1-D one does.
       The three DEGENERATE steps are skipped, and every identity holds whatever the stride turns out to be
       - so a host-owned axis folds exactly as a literal one does and nothing is baked. `0 * W + idx` is
       `idx`, which is the whole of `grid[0, x]`, the row-0 idiom; `acc * W + 0` is the multiply alone;
       `1 * W + idx` is `W + idx`, the row-1 one.
       Emitting them cost two and one RUNTIME instructions per access, inside whatever loop the subscript
       sat in. `subscriptStruct` has always skipped the flat `+ 0` (its `k !== '0'` test); this is the
       same rule reaching the axes. */
    mulAddAxis = function (acc, stride, idx, sourceCode, sourceOffset) {
        stride = '#' + dropHash(stride);                 /* an axis is a CONSTANT operand, not an address */
        if (indexKind(acc) === 'now' && constInt(acc) === 0) {
            returnBack(acc);
            return idx;                                           /* the caller owns it now, so it is NOT returned */
        }
        var zero = (indexKind(idx) === 'now' && constInt(idx) === 0);
        var live = (indexKind(acc) === 'runtime'
                || (!zero && indexKind(idx) === 'runtime'));
        var one = (indexKind(acc) === 'now' && constInt(acc) === 1);
        if (one && zero) {                                        /* `1 * stride + 0` IS `stride`, and a minted
                                                                     name carries its own tier - see indexKind */
            returnBack(acc);
            returnBack(idx);
            return stride;
        }
        var dst = borrow(live ? '%' : '<');
        if (one) {                                                /* `1 * stride` IS `stride` - the THIRD degenerate
                                                                     step, and `a[1, x]` is as ordinary as row 0 */
            emit(live ? '+' : '<> +', 'i', dst, stride, idx);
        } else {
            emit(live ? '*' : '<> *', 'i', dst, acc, stride);
            if (!zero) {
                emit(live ? '+' : '<> +', 'i', dst, dst, idx);
            }
        }
        returnBack(acc);
        returnBack(idx);
        return (live ? dst : '#' + dst);
    };

    /* Reduce a multidimensional subscript to the ONE linear index every path below already takes. Axes are
       numbered by STRIDE, innermost first, so `a[y, x]` on `[H, W]` is `y * W + x` - Horner from the
       outermost axis inwards, which is one MUL+ADD per extra axis and folds entirely at assembly time when
       the indices are constant. The per-axis extents come from the place's `dims`, which only a struct
       ARRAY FIELD carries today (slice 1); anything else has no shape to stride by and says so. */
    /* ONE AXIS dressed as an ordinary extent, which is what lets every tier check it VERBATIM instead of
       growing a second, divergent set of rules - only the NAME differs, so the message says which axis
       rather than just naming the array. An unresolvable `n` (a HOST-OWNED axis: `.d.H.cells.0` names it
       and only the assembler can read it) is already what `checkConstIndex` treats as tier 2, so the
       deferral decision 2 promises arrives with no branch of its own.
       It deliberately carries NO `dims` of its own: `dims` is the flag that means "a shape, whose flat
       product is implied by its axes", and an axis record wearing one would have its own checks skipped.
       Named rather than written inline because that contract is otherwise invisible - and because it was
       written inline TWICE, once per tier, which is how the two came to be edited separately. */
    axisExtent = function (ext, axis) {
        return { n: ext.dimN[axis], what: ext.what + ' axis ' + axis,
                 sym: ext.dims[axis], inField: true };
    };

    /* One axis of a shaped subscript, checked against that axis's own extent - both static tiers and the
       runtime one, in the order they must run, which is exactly what `checkSubscript` already is. */
    checkAxis = function (slot, axis, idxRV, oobs, sourceCode, sourceOffset) {
        var ext = slot.extent;
        if (ext === undefined || ext.dims === undefined) {
            return;                                               /* no shape - nothing to check per axis */
        }
        var oob = checkSubscript(axisExtent(ext, axis), idxRV,
                sourceCode, sourceOffset);
        if (oob !== undefined) {
            oobs.push(oob[0]);                                    /* every axis keeps its own finding - see
                                                                     checkIndexUse for why not arbitrated */
        }
    };

    /* A subscript states EVERY axis of the array it indexes - asked of every subscript, including the
       ones with no comma in them. Asking only when a comma was written left the flat spelling as an open
       door back into exactly what a shape exists to reject: `cells[11]` on a `[3, 4]` is a legal WORD
       offset and an illegal coordinate, and it compiled silently. An array with no `dims` is rank 1, so
       an ordinary array - and a bare pointer, which has no extent at all - still takes one index and is
       unaffected. */
    checkRank = function (slot, count, sourceCode, sourceOffset) {
        var dims = slot.extent && slot.extent.dims;
        var rank = (dims === undefined ? 1 : dims.length);
        if (rank !== count) {
            fail('This array has ' + (rank === 1 ? 'one axis' : rank + ' axes')
                    + ', but the subscript names ' + count,
                    sourceCode, sourceOffset, 'E206',
                    'a subscript states every axis of the array it indexes');
        }
    };

    /* `oobs` collects the per-axis findings for the ONE caller that then hands them to the lowering. It is
       a parameter and not parser state: producer and consumer are three lines apart in the `Subscript`
       rule, and a parser field had to be drained by every lowering or it leaked one subscript's axes into
       the next - a hazard a local array cannot have. */
    foldAxes = function (slot, first, extra, extraAt, oobs, sourceCode, sourceOffset) {
        var dims = slot.extent.dims;                              /* checkRank already agreed on the count */
        var acc = makeRValue(metaSlot(first));
        /* EVERY axis is checked against ITS OWN extent, which is the entire safety argument for shapes:
           the flat product cannot catch `cells[0, W]`, an index that stays inside the allocation while
           walking off the end of its row. Written order is outermost-first, so written index j is axis
           `rank-1-j`. Reuses the ordinary index check with a per-axis extent record, so a shape inherits
           `E461` and the deferred tier verbatim rather than growing a second, divergent set of rules. */
        var rank = dims.length;
        checkAxis(slot, rank - 1, acc, oobs, sourceCode, sourceOffset);
        for (var a = 0; a < extra.length; ++a) {
            checkAxis(slot, rank - 2 - a, extra[a], oobs, sourceCode, extraAt[a]);
            /* stride of the axis we are LEAVING = the extent of the next axis inwards */
            acc = mulAddAxis(acc, dims[rank - 2 - a], extra[a]);
        }
        /* The flat `.z.` extent is now SUPERSEDED, and saying so once here is the whole of it: every tier
           below reads `extent === undefined` as "nothing to check", so none of them needs to know what a
           shape is. Sound because every axis was just checked and `.z.` is their product, and honest
           because it is stated where that becomes true. Two inline `dims === undefined` opt-outs used to
           say it in two tiers, while the third only ever skipped by accident - the folded index happens to
           be a scratch, which `checkConstIndex` returns early on. Clearing the SLOT's reference; the
           cached per-field record it points at is untouched. */
        slot.extent = undefined;
        return wrapOperand(acc);
    };

    subscriptStruct = function (x, idx, sourceCode, sourceOffset) {
        x = metaSlot(x);
        var extent = x.extent;
        if (!x.arrayOf) {                                         /* a raw struct pointer: wrap it as an array place */
            var p = makeRValue(x);
            setPlace(x, (p[0] === '&' ? 'globalAddr' : 'pointer'), p, [], undefined, x.elem);
        }
        var elem = x.arrayOf;
        var elemStruct = isStructAtom(elem);            /* struct element -> stride .z.elem, a place for the next .field;
                                                                    scalar element -> stride 1 word, this [k] IS the terminal value */
        var eType = descHead(elem), eTail = descTail(elem);
        var elemName = (elemStruct ? descName(elem) : undefined);   /* `.z.`/place slots want the NAME */
        var idxRV = makeRValue(metaSlot(idx));

        var oobIndex = checkConstIndex(extent, idxRV, sourceCode, sourceOffset);

        var idxKind = indexKind(idxRV);
        if (idxKind !== 'runtime') {                             /* an assemble-time index -> fold into the compile-time offset parts */
            /* This test used to be `/^#[0-9]+$/`, so a named const, a NEGATIVE literal and a folded `<A>`
               all fell to the branch below and emitted GETL/SETL with an immediate index - an encoding
               that does not exist, so a module the compiler accepted would not assemble. */
            var scratch = (idxKind === 'scratch');
            var k = dropHash(idxRV);
            if (!scratch) {
                returnBack(idxRV);                      /* a scratch passes to offParts, whose fold frees it */
            }
            if (k !== '0') {
                var part = k;                                    /* scalar stride is 1 word -> the offset is just k */
                if (elemStruct) {                                /* `&p[1]` is the canonical walk, so the fold
                                                                    inside scaleByStride is the common case here */
                    part = scaleByStride(k, extentSymbol(elemName));
                    if (scratch) { returnBack(k); }     /* a folded k is a literal, never a scratch */
                }
                /* A folded `<X>` cannot key a deferred assertion, so the guard takes its OWN copy while the
                   value is still live - the pushed one is freed by foldOffset long before the use decides
                   whether this is a dereference at all. One assemble-time MOVi, no runtime cost, and the
                   copy is returned by whichever side consumes the finding. A SHAPE never reaches here with
                   an extent at all - foldAxes cleared it, having checked strictly more per axis. */
                if (scratch && extent !== undefined && extent.inField) {
                    var g = borrow('<');
                    emit('<> =', 'i', g, '#' + part, undefined);
                    oobIndex = { k: g, own: true, ext: extent, copyAt: metacode.length - 1,
                            src: sourceCode, off: sourceOffset };
                }
                x.offParts.push(part);
            }
            if (elemStruct) setPlace(x, x.baseKind, x.base, x.offParts, elemName, undefined, x.dynIndex);
            else            emitPlaceValue(x, x.baseKind, x.base, x.offParts, x.dynIndex, eType, eTail);

        } else if (x.baseKind === 'local' && x.dynIndex === undefined) {
            /* a frame place with a single runtime index: keep it frame-relative so it emits one
               GETL/SETL (dsp + constOff)[idx], not ADRL + ADDp + PEEK/POKE (struct index is scaled;
               scalar is 1). Only a genuinely RUNTIME index reaches here - every assemble-time one, named
               or negative, folded above, because GETL/SETL have no immediate-index form. */
            if (elemStruct) {
                var frameIdx = borrow('%');
                emit('*', 'i', frameIdx, idxRV, '#' + extentSymbol(elemName));
                emitRangeCheck(frameIdx, extent, sourceCode, sourceOffset);   /* scaled: `.z.` counts words */
                returnBack(idxRV);
                setPlace(x, 'local', x.base, x.offParts, elemName, undefined, frameIdx);
            } else {
                emitRangeCheck(idxRV, extent, sourceCode, sourceOffset);
                emitPlaceValue(x, 'local', x.base, x.offParts, idxRV, eType, eTail);
            }

        } else {
            var arrPtr = placeAddress(x);                /* pointer base or a second runtime index: materialize */
            if (elemStruct) {
                var elemPtr = borrow('%'), scaled = borrow('%');
                emit('*', 'i', scaled, idxRV, '#' + extentSymbol(elemName));
                emitRangeCheck(scaled, extent, sourceCode, sourceOffset);
                emit('+', 'p', elemPtr, arrPtr, scaled);
                returnBack(scaled);
                returnBack(idxRV);
                returnBack(arrPtr);
                setPlace(x, 'pointer', elemPtr, [], elemName);
            } else {                                              /* scalar stride 1 -> PEEK/POKE arrPtr idx directly */
                emitRangeCheck(idxRV, extent, sourceCode, sourceOffset);
                emitPlaceValue(x, 'pointer', arrPtr, [], idxRV, eType, eTail);
            }
        }
        x.oobIndex = (oobIndex === undefined ? undefined : [ oobIndex ]);
                                                                  /* AFTER the terminal call on every path -
                                                                     setPlace and emitPlaceValue both clear
                                                                     it. A LIST - see checkIndexUse */
    };

    /* a place's address: fold its offset parts into the base - ADRL for a local, ADDp for a pointer. */
    placeAddress = function (place) {
        place = metaSlot(place);
        var off = foldOffset(place.offParts);
        var a;
        if (place.baseKind === 'local') {                         /* size hint = the pointed-at sub-object, not the enclosing frame */
            var sz = '*0';                                        /* ALWAYS `*0` - see the note on ADRL spans above. */
            a = borrow('%');
            emit('=&', 'p', a, place.base + (off ? ':' + off : ''), sz);
            if (place.dynIndex !== undefined) {                   /* fold the frame place's runtime index in (GETL/SETL fallback) */
                emit('+', 'p', a, a, place.dynIndex);
                returnBack(place.dynIndex);
                place.dynIndex = undefined;
            }
        } else {                                                  /* pointer / globalAddr */
            if (!off) return place.base;
            a = borrow('%');
            emit('+', 'p', a, place.base, '#' + off);
        }
        if (off && ('' + off).charAt(0) === '<') returnBack(off);
        return a;
    };

    /* ADRL SPANS: `*0` unless the compiler owns every access through the pointer.
       ------------------------------------------------------------------------------------------
       `ADRL base *N` is defined by the JIT's aliasing rule (design/jit/JitAliasingRegAlloc.md on the
       jit-compiler branch) as "this pointer accesses exactly [base, base+N)" - a REACH BOUND, trusted
       by the optimizer. `*0` means "assume nothing, anything here can alias", which is what every
       program emits today and what the JIT will keep treating conservatively.

       Impala can only honour a reach bound where the pointer cannot be moved. It usually can be: a
       struct pointer is moved with `&p[k]` (plain `p + 1` is E307), which lowers to `ADDp`, and
       pointer indexing is unchecked BY DESIGN. So `p = &grid[0]` followed by the documented
       `while (p < end) { p = &p[1]; }` walk provably leaves any object-sized span - and this used to
       emit `*.z.Cell`, a bound the program breaks two lines later. Harmless while nothing reads the
       operand; a JIT-only miscompile the moment one does, in an artifact that may already have
       shipped, since a `.gazl` is text that outlives the compiler that wrote it.

       So user-visible address-of (`&s`, `&arr[i]`, `&x`) emits `*0`, always. The two sites below are
       the exception and the reason the rule is phrased around OWNERSHIP rather than around types: the
       compiler emits the ADRL, emits the single COPY that is its only use, and releases the temp -
       nothing else can reach it, so the bound is true by construction.

       by-value struct argument: reserve `words` window slots at %winSlot and COPY the
       struct value in (experiment-verified: ADRL the window region, ADRL the source, COPY). */
    copyStructArg = function (argMeta, winSlot, words) {
        argMeta = metaSlot(argMeta);
        for (var k = 0; k < words; ++k) {                         /* reserve the window slots */
            claimSlot(winSlot + k);
        }
        /* Size hints here stay NUMERIC on purpose (not *.z.Struct): the window is a fixed `words`-slot
           block baked by the register allocator, and these must match it exactly - a symbolic size that
           later resolved differently would overrun the window. By-value locks the size; an extern struct,
           whose size is host-owned, could not supply one - which is why by-value and extern were mutually
           exclusive before by-value was parked entirely. See design/gazl/GAZLSymbolicWindows.md. */
        var dst = borrow('%');
        emit('=&', 'p', dst, '%' + winSlot, '*' + words);   /* ADRL address of the window region */
        var src = placeAddress(argMeta);                    /* address of the source struct */
        emit('copy', '?', dst, src, '*' + words);           /* COPY dst src *words (matches the window) */
        returnBack(src);
        returnBack(dst);
    };

    fieldAccess = function (x, fieldName, arrow, sourceCode, sourceOffset) {
        x = metaSlot(x);

        var bk, base, offParts, structName, dynIndex;
        if (x.place) {
            if (arrow) {
                fail("Use '.' - this is a struct value, not a pointer", sourceCode,
                        sourceOffset, 'E416', 'write ' + fieldName + ' as .' + fieldName);
            }
            bk = x.baseKind; base = x.base; offParts = x.offParts || []; structName = x.struct;
            dynIndex = x.dynIndex;
        } else if (x.type === 'p' && isStructAtom(x.elem)) {
            if (!arrow) {
                fail("Use '->' to access a field through a pointer", sourceCode,
                        sourceOffset, 'E416', 'write ' + fieldName + ' as ->' + fieldName);
            }
            bk = 'pointer'; base = makeRValue(x); offParts = []; structName = descName(x.elem);
        } else {
            fail("Field access requires a struct" + (arrow ? ' pointer' : ''),
                    sourceCode, sourceOffset, 'E415');
        }

        checkIndexUse(x);                                 /* `e[9].a` reaches INTO an element that is not there */

        var field = findField(structName, fieldName);
        if (!field) {
            fail('Struct ' + structName + ' has no field ' + fieldName,
                    sourceCode, sourceOffset, 'E417');
        }
        /* The place carries a list of compile-time offset PARTS (field-offset symbols). A field just
           appends its `.o.<struct>.<field>` part - nested structs accumulate parts with ZERO
           instructions; the parts are folded (inline `! ADDi`, assemble-time) into one operand only at
           a terminal access. Only accessed paths cost anything, and never at run time. */
        var fieldSym = fieldSymbol(structName, fieldName);
        noteStructUse(structName, sourceCode, sourceOffset);
        var newParts = offParts.concat([fieldSym]);

        if (field.type === 'S') {                                 /* nested struct -> accumulate the part, same base, NO instruction */
            setPlace(x, bk, base, newParts, field.struct, undefined, dynIndex);
            return;
        }
        if (field.type === 'A') {                                 /* array field -> a fold-able array place for the next [k]
                                                                     (struct OR scalar element; subscriptStruct terminates it) */
            setPlace(x, bk, base, newParts, undefined, field.elem, dynIndex);
            if (field.extent === undefined) {                      /* built once per FIELD, not per access */
                field.extent = arrayExtent(fieldName, structName,
                        structName + '.' + fieldName, field.size, field.dims);
                field.extent.inField = true;                      /* an overrun stays inside the struct's own
                                                                     allocation, so nothing else looks */
var _sn = strideStruct(field.elem);
                field.extent.stride = (_sn !== undefined ? extentSymbol(_sn) : undefined);
            }
            x.extent = field.extent;
            return;
        }

        /* terminal scalar field */
        emitPlaceValue(x, bk, base, newParts, dynIndex, field.type, field.elem);
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
        if (leftx.type === 't' && isFuncTypeName(leftx.elem)) {   /* named funcptr type target */
            checkFuncPtrTarget(leftx.elem, bareOperand(rightx), rightx.type, rightx.elem,
                    '', sourceCode, sourceOffset);
        }
    };

    /* The lone operand of a one-operand meta - a direct `&f` / `^f` reference or `&NULL` - and
       undefined for anything compound, which is neither. */
    function bareOperand(x) {
        return (x.operands && x.operands[2] === undefined ? x.operands[1] : undefined);
    }

    /* A named funcptr target, at an assignment or at an argument (`at` names which): `nullfunc` suits
       any type, a direct function reference is checked against it, and anything else must ALREADY
       carry that exact type. An untyped funcptr is not silently promoted, for the same reason a bare
       `pointer` is not assignable to an `int pointer` (E201) - it guarantees nothing about the shape
       of what gets called. The cast that says "I checked" needs no `pointer`, a functype being one. */
    checkFuncPtrTarget = function (expected, operand, actualType, actualElem, at,
                                            sourceCode, sourceOffset) {
        if (operand === '&NULL') {
            return;
        }
        /* Gated on the LOOKUP, not on the sigil: reading a global spells itself `&name` too, so
           `&`/`^` alone cannot tell a function reference from one, and testing the sigil sent every
           global funcptr into this branch to find no function and silently fall out - past the
           element check below, which is the one that applies to it. */
        var fe = (operand && (operand.charAt(0) === '&' || operand.charAt(0) === '^')
                ? symbols.functions[operand.substr(1)] : undefined);
        if (fe && fe.signature) {                                 /* a direct function reference */
            if (!funcTypeMatches(expected, fe.signature)) {
                fail('Function ' + operand.substr(1) + " does not match funcptr type '"
                        + expected + "'" + at, sourceCode, sourceOffset, 'E441',
                        'check the parameter and return types against ' + expected);
            }
        } else if (actualType === 't' && actualElem !== expected) {
            fail('Funcptr type mismatch' + at + " (expected '" + expected + "', got "
                    + (actualElem !== undefined && isFuncTypeName(actualElem)
                            ? "'" + actualElem + "'" : 'an untyped funcptr') + ')',
                    sourceCode, sourceOffset, 'E441', 'use a cast: (' + expected + ')');
        }
    };

    /* A cast BETWEEN named funcptr types is only honest when the shapes agree - unlike a data pointer,
       the shape decides how a call through the result reads its frame, so a mismatched retype is a
       wrong call from the moment it is made. Untyped `funcptr` stays the escape hatch: `(funcptr)x`
       erases the name (a base-type cast erases the element) and a named cast re-stamps it, so the
       deliberate conversion is spelled `(To)(funcptr)x` - visible, and greppable. */
    checkFuncTypeCast = function (x, toName, sourceCode, sourceOffset) {
        var m = metaSlot(x);
        if (m.type !== 't' && m.type !== '?') {                   /* unknown is not over-reported. A NATIVE is not a
                                                                     funcptr either: `MOVp` has no `^native` form, so the
                                                                     value cannot even be stored - only called by name */
            fail('Only a funcptr can take a funcptr type - this is '
                    + (VERBOSE_TYPES[m.type] || 'not one'), sourceCode, sourceOffset, 'E465',
                    (m.type === 'N' ? 'a native can only be called directly, by name'
                            : 'a funcptr value only comes from a function name, nullfunc, or another funcptr'));
        }
        var fromName = (m.type === 't' ? m.elem : undefined);
        if (fromName === undefined || fromName === toName || !isFuncTypeName(fromName)) {
            return;                                               /* untyped or same: no shape claim to compare */
        }
        var got = signatureShape(fromName, functypes[fromName]);
        var want = signatureShape(toName, functypes[toName]);
        if (got.substr(fromName.length) !== want.substr(toName.length)) {
            fail('Cast between funcptr types of different shape ("' + got + '" vs "'
                    + want + '")', sourceCode, sourceOffset, 'E465',
                    'a call through the result would read the wrong frame - if you mean it, go through '
                            + 'untyped funcptr: (' + toName + ')(funcptr)...');
        }
    };

    /* An INITIALIZER states exactly what an assignment states, so it answers to the SAME function - it
       just reached the data rows through makeConstant, which knows a type and neither a funcptr type nor
       a pointer element. Both halves were silent at every declaration door: `global TickFn onTick = wrong`
       stored a function of any shape under a name promising one shape, and `global int pointer p =
       &global f[0]` off a `float array` published `: int-ptr` over float storage - while the in-function
       twin of each is E441 / E201. checkPtrAssign, not a wrapper around half of it, so a third check
       added there cannot be right for assignments and missing at declarations. */
    checkInitTarget = function (type, elem, x, sourceCode, sourceOffset) {
        if (type === undefined) {
            return;
        }
        var m = metaSlot(x);
        /* Ask about the ELEMENT only once the value is a pointer at all. `int pointer p = 1` is not an
           element mismatch - "got untyped elements" would be a worse answer than the true one - and the
           assignment path never has to say so, because E303 stops a non-pointer before checkPtrAssign
           runs. Here the coarse question belongs to makeConstant, which answers it with E407. */
        if (type === 'p' && m.type !== 'p') {
            return;
        }
        checkPtrAssign({ type: type, elem: elem }, m, sourceCode, sourceOffset);
    };

    /* --------------------------------------------------------- *
     *  Binary operations ( + - * / [] etc. )                    *
     * --------------------------------------------------------- */
    binaryOp = function (operator, leftx, rightx,
                                  sourceCode, sourceOffset) {

        leftx  = metaSlot(leftx);
        rightx = metaSlot(rightx);

        var lelem = leftx.elem;                                   /* element type of the base (Impala 2) */
        var lro   = leftx.readonly;                               /* a readonly array's element is readonly too */

        /* a binary result is never a verbatim call result; drop callInfo so
           assign() cannot mistake it for one (see unaryOp for rationale). */
        leftx.callInfo  = undefined;
        rightx.callInfo = undefined;

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
            /* A plain array reaches here rather than subscriptStruct, because it decayed to a pointer at
               lookup - so the extent rides on the meta instead of on a place. Same rule as there: only a
               DYNAMIC index gets the runtime test. A constant one is already an assemble-time offset and
               GAZL rejects it outright ("Offset out of bounds"), which is a better diagnostic than a trap.
               The two branches below are exactly the two shapes that name a declared array, `&global` and
               `$local`; anything else is a bare pointer with no extent to check. */
            var xt = leftx.extent;
            var xOob;

            /* `&global a[i]` and `$local a[i]` differ only in which pair of operators they emit: a global
               base is dereferenced (`=*` / PEEK), a frame base is read directly (`=` / GETL). Everything
               else - r-valuing the index, the bounds check, and folding an assemble-time index into a
               `base:offset` operand - is one rule, and used to be written out twice. */
            var direct = (leftx.operator === '=&');
            if (direct || (leftx.operator === ':=' && op1[0] === '&')) {
                assert(!direct || op1[0] === '$', "=& expects local '$'");
            
                var op2 = makeRValue(rightx);
                xOob = checkSubscript(xt, op2, sourceCode, sourceOffset);
            
                if (op2[0] === '#' || op2[0] === '<') {          /* assemble-time: fold into `base:offset` */
                    makeMeta(leftx, (direct ? '=' : '=*'), tp, null,
                                      op1 + ':' + (op2[0] === '#' ? op2.substr(1) : op2), null);
                } else {
                    makeMeta(leftx, (direct ? '=[]$' : '=[]'), tp, null, op1, op2);
                }
            

            } else {
                /* general indexed read */
                makeMeta(leftx, operator, tp, null,
                                  makeRValue(leftx),
                                  makeRValue(rightx));
            }
            leftx.oobIndex = xOob;                    /* after the makeMeta above, which clears it */

        } else {

            /* pointer-difference special-case “d” */
            var diff = (operator === '-' && (rightx.type === 'p' || rightx.type === 't'));
            if (diff) {
                var funcDiff = (rightx.type === 't');             /* ordinals, not addresses: no stride, nothing to match */
                operator = 'd';
                /* The difference counts ELEMENTS (DIFp, then DIVi by the stride), so it only means
                   anything when both pointers walk the same element type - `ip - fp` would divide a
                   float-strided span by the int size. Same rule as assignment (E201); comparison is
                   left alone, it reads a raw address either way. */
                if (!funcDiff && lelem !== rightx.elem) {
                    fail('Pointer difference needs matching element types ('
                            + elemVerbose(lelem) + ' and '
                            + elemVerbose(rightx.elem) + ')',
                            sourceCode, sourceOffset, 'E201',
                            'subtract pointers into the same array, or cast one to match');
                }
            }

            /* Arithmetic on a struct pointer is REJECTED, and subscripting is how you move one: `&p[i]`.
               Scaling `+`/`-` instead was tried and reverted - it leaked into comparison (no unit there at
               all) and `for` could not honour it, `FORp` having no room for a stride. That reasoning is
               untouched by the removal of `[[ ]]` (2026-08-04): the subscript still spells the move, it
               just spells it the same way every other subscript does. Comparisons fall through untouched:
               they are unit-free and are what the `while` walk is built on. */
            var stride = (leftx.type === 'p' ? strideStruct(lelem) : undefined);
            if (stride !== undefined && (operator === '+' || operator === '-')) {
                fail('Arithmetic on a ' + stride + ' pointer', sourceCode, sourceOffset,
                        'E307', 'a struct pointer moves by scaled subscript only - write `&p[i]`');
            }
            if (stride !== undefined && diff) {
                fail('Difference between ' + stride + ' pointers', sourceCode, sourceOffset,
                        'E308', 'the element count is `((pointer)q - (pointer)p) / sizeof('
                                + stride + ')`');
            }
            makeMeta(leftx, operator, tp, null,
                              makeRValue(leftx), makeRValue(rightx));
        }

        leftx.readonly = lro;                                     /* survives the makeMeta calls above */

        /* element-type propagation (Impala 2) */
        if (operator === '=[]' && lelem !== undefined) {
            var el = descTypeElem(lelem);                /* typed element read: no cast needed */
            leftx.type = el.type;                                 /* a funcptr-array element carries its funcptr type */
            leftx.elem = el.elem;
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
        checkIndexUse(leftx);                             /* writing it: a target is never an address */
        checkIndexUse(rightx);                            /* `x = s.v[2]` reuses the operand without makeRValue */

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
            /* This path returns before the scalar `readonly` check below, so a whole-struct COPY into a
               `readonly` global used to be emitted straight into the const region. */
            if (leftx.readonly === true) {
                fail('Cannot assign to a readonly value', sourceCode, sourceOffset, 'E404',
                        'declare it `global` instead of `readonly` if it has to be written');
            }
            var savedBK = leftx.baseKind, savedBase = leftx.base,
                savedParts = leftx.offParts, savedStruct = leftx.struct;
            var dst = placeAddress(leftx);
            var src = placeAddress(rightx);
            makeMeta(x, 'copy', '?', dst, src, structAllocSize(leftx.struct));
            emitMeta(x);
            returnBack(src);
            returnBack(dst);
            setPlace(x, savedBK, savedBase, savedParts, savedStruct);
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
                /* Reads as a statement about the assignment, not as an equation: `(pointer = funcptr)`
                   left people working out which side was which. Source first, destination second. */
                'Incompatible types for assignment: cannot assign {$type2} to {$type1}',
                sourceCode, sourceOffset,
                leftx.type, rightx.type
            , 'E303');
        }

        checkPtrAssign(leftx, rightx, sourceCode, sourceOffset);

        /* `readonly` was honoured for scalars and silently ignored for an indexed write, which left the
           assembler to catch it as `Incompatible types` against the const region - if at all. The FLAG
           is the whole test: every readonly branch (scalar global, array element, struct field) sets it,
           and nothing else does, so a literal or a function name falls through to `Invalid lvalue`. */
        if (leftx.readonly === true) {
            fail('Cannot assign to a readonly value',
                    sourceCode, sourceOffset, 'E404',
                    'declare it `global` instead of `readonly` if it has to be written');
        }

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

        } else if (lop === '=[]' || lop === '=[]$') {   /* the store form of each: PEEK->POKE, GETL->SETL */

            makeMeta(
                x, (lop === '=[]' ? '[]=' : '[]$='), rightx.type,
                leftx.operands[1],
                leftx.operands[2],
                makeRValue(rightx)
            );

        } else {
            fail('Invalid lvalue', sourceCode, sourceOffset, 'E404');
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
     *  Unary helpers  (dereference, reference, -, ~, abs/floor,
     *                  int↔float conversions)
     * -------------------------------------------------------- */

    /* *expr  or  [] dereference handling */
    dereference = function (operator, expr, sourceCode, sourceOffset) {
        expr = metaSlot(expr);
        /* `constInt` is THE decoder: `parseFloat('0x10')` is 0, so a hex offset silently became `#0`
           here while the decimal spelling of the same number emitted `#-16`. */
        var negOff = (expr.operator === '-' && expr.operands[2] !== undefined
                ? constInt(expr.operands[2]) : undefined);
        if (expr.operator === '+') {
            /*  &a + i   ->   PEEK (&a , i)  */
            expr.operator = '=[]';
        } else if (negOff !== undefined) {
            /*  &a - #n  where n is const -> adjust to negative literal */
            expr.operator = '=[]';
            expr.operands[2] = '#' + (-negOff);
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
        for (var oi = 0; expr.oobIndex !== undefined && oi < expr.oobIndex.length; ++oi) {
            var oob = expr.oobIndex[oi];
            if (!oob.own) continue;
            returnBack(oob.k);              /* the guard's copy dies with the finding - and so does
                                                        the `! MOVi` that made it, or an address would ship
                                                        a line nothing reads (flushMetaCode skips a null) */
            metacode[oob.copyAt].operator = null;
        }
        expr.oobIndex = undefined;                   /* address formation is never bounds-checked, at any
                                                        index - see checkConstIndex. Cleared before the
                                                        `=[]$` branch below calls makeRValue. */

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
            fail('Invalid lvalue', sourceCode, sourceOffset, 'E404');
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
            setPlace(expr, 'pointer', makeRValue(expr), [], descName(expr.elem));
            return;
        }

        /* &structValue -> a typed struct pointer (the place's address); &arrayPlace -> a pointer to its
           ELEMENT. setPlace leaves `struct` undefined for an array place (it fills `arrayOf` instead), so
           taking the struct branch unconditionally minted the descriptor `Sundefined` - which
           `isStructAtom` accepts and renderDesc unwraps to the word "undefined", so `p = &global b.tags`
           on `struct Body { int array tags[4] }` failed with "expected int elements, got undefined
           elements". `arrayOf` is already the element descriptor, which is what makeRValue uses for the
           same decay. */
        if (operator === '&' && expr.place) {
            var structName = expr.struct, arrayElem = expr.arrayOf;   /* both read BEFORE makeMeta clears the place */
            if (expr.baseKind === 'local' && (!expr.offParts || expr.offParts.length === 0) && expr.dynIndex === undefined) {
                /* a whole local's address is a single ADRL with no offset scratch - leave it DEFERRED as
                   '=&' so an assignment emits ADRL straight into its target ($p) instead of a temp + MOVp,
                   exactly like &scalar / &array[i] defer in reference() */
                var sz = '*0';                                /* ALWAYS `*0` - see the note on ADRL spans above. */
                makeMeta(expr, '=&', 'p', undefined, expr.base, sz);
            } else {                                          /* offset fold or global/pointer base: materialize now */
                makeMeta(expr, ':=', 'p', undefined, placeAddress(expr), undefined);
            }
            setElem(expr, (arrayElem !== undefined ? arrayElem
                    : (structName !== undefined ? structDesc(structName) : undefined)));
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
            /* &x yields a pointer to x's type - but `?` (an element of an Impala 1 untyped
               `array`) says exactly what `undefined` says, and UNKNOWN MUST HAVE ONE SPELLING:
               with two, a comparison of the same non-knowledge reports itself as
               "expected untyped elements, got untyped elements". A named funcptr needs no case of
               its own: `t` + its name IS the descriptor. */
            expr.elem = (prevType === undefined || prevType === '?' ? undefined
                    : prevType + (prevElem !== undefined ? prevElem : ''));
        } else {
            expr.elem = undefined;                                /* casts and numeric ops erase */
        }

        /* a cast/deref/unary result is no longer a verbatim call result,
           so drop any callInfo that would let assign() pin the callee's
           return type to the l-value (e.g. *(pointer)f() must not make f
           "expect" the deref's target type). */
        expr.callInfo = undefined;
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
            claimTopName(name, TOP_KINDS[scope], sourceCode, sourceOffset);
            var table = symbols[scope];
            var prev  = ownEntry(table, name);

            if (prev) {
                if (kind !== undefined && prev.kind !== undefined) {
                    /* a local is TABLED as `$b`; the user wrote `b`, so report what they wrote */
                    fail('Identifier already declared: '
                                          + (name.charAt(0) === '$' ? name.substr(1) : name),
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
                externProto: prev && prev.externProto,   /* survives the definition's signature reset */
                extent: prev && prev.extent,             /* ditto: this REBUILDS the record, so an array's
                                                            extent must be carried or a later `extern array g`
                                                            - the ordinary import-closure shape - drops it and
                                                            silently disarms E461 and --range-checks */
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
        var nextRide    = false;    // ...and whether it may ride a `!` line instead of costing a NOOP
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
                nextRide  = (rec.mayRide === true);
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

                /* A label RIDES this `!` line only if its minter said so. The assembler resolves
                   `! LSSi .. @L` against a line that folds away, but a runtime `GOTO @L` then reports
                   "Symbol not found (in expected scope)" - and a label nobody operands at all (a switch
                   case, which SWCH spells out of a table base) would vanish entirely. Only the minter
                   knows which it is, so it flags the record rather than have this pass guess. */
                if (nextLabel !== TABstr && !nextRide) {
                    output(prefix + nextLabel + 'NOOP');
                    nextLabel = TABstr;
                }

                var gop = META_TO_GAZL[ op.substr(3) ];
                gop      = replace(gop, '?',
                                   TYPE_SUFFIXES[ rec.type ]);

                output(prefix + nextLabel + '! ' + gop + ' ' +
                       formatOperand(rec.operands[0]) + ' '   +
                       formatOperand(rec.operands[1]) + ' '   +
                       formatOperand(rec.operands[2]) + nextComment);

                nextLabel   = TABstr;
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
                setPlace(x, 'local', '$' + name, [], descName(p.elem));
                return;
            }
            if (p.type === 'A' && isStructAtom(p.elem)) {   /* struct-element array -> a foldable local array place (base:offset, no ADRL) */
                setPlace(x, 'local', '$' + name, [], undefined, p.elem);
                x.extent = p.extent;                                  /* after setPlace, which clears it */
                return;
            }
            if (p.type === 'A') {
                makeMeta(x, '=&', 'p', undefined,
                                  '$' + name, '*0');
                x.extent = p.extent;                                  /* after makeMeta, which clears it */
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
        if (isGlobal && (p = ownEntry(sym.globals, name))) {

            if (p.type === 'S') {                                 /* struct value global -> a place in global memory */
                setPlace(x, 'globalAddr', '&' + name, [], descName(p.elem));
                metaSlot(x).readonly = (p.readonly === true);      /* a field write must see it too - CNST
                                                                     rejects the POKE only at load, if at all */
                return;
            }
            if (p.type === 'A') {
                makeMeta(x, ':=', 'p', undefined,
                                  '&' + name, undefined);
                x.extent = p.extent;                                  /* after makeMeta, which clears it */
                /* A scalar `readonly` is rejected by its `:=*` operator having no assign() branch, but an
                   ARRAY decays to the same `:=` address meta whether or not it is writable - so carry the
                   flag and let assign() consult it once the subscript has been folded in. */
                x.readonly = (p.readonly === true);
            } else {
                makeMeta(x,
                                  (p.readonly ? ':=*' : '=*'),
                                  p.type,
                                  undefined,
                                  '&' + name,
                                  undefined);
                x.readonly = (p.readonly === true);               /* makeMeta clears it; assign() reads
                                                                     the FLAG, not the operator spelling */
            }
            setElem(x, p.elem);
            return;
        }

        /* `global` names a storage table, and neither a function nor a const is in one - so the two
           branches below used to ignore the flag entirely and the keyword was silently discarded. */
        var asFn  = ownEntry(sym.functions, name);
        var asDef = ownEntry(sym.defines, name);
        if (isGlobal && (asFn || asDef)) {
            strictError('`global` is only for global variables - ' + name + ' is a '
                            + (asFn ? 'function' : 'constant'),
                    sourceCode, sourceOffset, 'E452', 'drop the `global` keyword');
        }

        /* function ---------------------------------------------*/
        if ((p = asFn)) {
            if (p.type === 'N') {
                makeMeta(x, ':=', 'N', undefined,
                                  '^' + name, undefined);
            } else {
                assert(p.type === 'U', 'function entry must be U');
                makeMeta(x, ':=', 't', undefined,
                                  '&' + name, undefined);
            }
            setElem(x, undefined);
            return;
        }

        /* constant / #define -----------------------------------*/
        if ((p = asDef)) {
            makeMeta(x, ':=', p.type, undefined,
                              '#' + name, undefined);
            setElem(x, p.elem);
            return;
        }

        /* not found --------------------------------------------*/
        /* Before giving up, look in the OTHER namespace. `global` is a mandatory prefix at every use
           site, so reading a global without it - or a local/const with it - is the first error most
           newcomers and code generators hit, and a bare "Undeclared identifier" points away from the
           one-word fix. The symbol tables already hold the answer; say it. */
        var hint;
        if (!isGlobal && hasOwn(sym.globals, name)) {
            hint = name + ' is a global - write `global ' + name + '`';
        } else if (isGlobal && sym.locals['$' + name]) {
            hint = name + ' is a local - drop the `global` keyword';
        }
        fail('Undeclared identifier: ' + name,
                      sourceCode, sourceOffset, 'E403', hint);
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

        /* `constInt` is THE decoder - asking the spelling here is how `0x10` stopped folding while
           `16` folded (it read as decimal-only and fell through to a runtime temp). */
        var subL = constInt(opL), subR = constInt(opR);
        if (subR === 0) return opL;                               /* trivial case, in any spelling */
        if (subL !== undefined && subR !== undefined) {
            return '#' + (subL - subR);
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
        topNames         = {};
        guardCounter     = 0;
        holdCounter      = 0;
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
};function root($){return (function(){var _b=_i;return _($)&&(function(){ start(); ; return true})()&&((function(){while((function(){var _b=_i;return (function(){ declOffset = _i; declSource = _s; ; return true})()&&(function(){var _b=_i;return ImportDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExportDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||StructDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FuncTypeDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ExternDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _l=_i,_x=(!!_s[_i])&&(++_i,true);_i=_l;return !_x})()&&(function(){ end(); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ImportDecl($){var $path=createParserContext();return (function(){var _b=_i;return IMPORT($)&&_($)&&StringLiteral($path)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExportDecl($){return (function(){var _b=_i;return EXPORT($)&&_($)&&(function(){ exportNext = true; ; return true})()&&(function(){var _b=_i;return FuncDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||GlobalDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)||ConstDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ exportNext = false; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StructDecl($){var $nmAt,$id=createParserContext(),$sname,$f=createParserContext();return (function(){var _b=_i;return STRUCT($)&&_($)&&(function(){ $nmAt = _i; ; return true})()&&Identifier($id)&&(function(){ $sname = $id._; beginStruct($id._, _s, $nmAt); ; return true})()&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return ArrayDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ addStructField($sname, { name: $f.name, type: $f.type, elem: $f.elem, struct: $f.struct, size: $f.size, dims: $f.dims }, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){   endStruct($sname); /* Publish the real layout with TYPES so a signature-row consumer can check an `extern struct` declaration of the same name against it - the .o./.z. constants alone carry no types. */ emitStandaloneSignatureComment( structSignatureRow($sname, false, sourceName, _s, declOffset)); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncTypeDecl($){var $nmAt,$id=createParserContext(),$ftname,$p=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return FUNCTYPE($)&&_($)&&(function(){ $nmAt = _i; ; return true})()&&Identifier($id)&&(function(){ $ftname = $id._; beginFuncType($id._, _s, $nmAt); ; return true})()&&(_s[_i]==="(")&&(++_i,true)&&_($)&&((function(){var _b=_i;return TypeDeclr($p)&&(function(){ addFuncTypeParam($ftname, $p.type, $p.elem, $p.struct, $p.words, $p.name, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($p)&&(function(){ addFuncTypeParam($ftname, $p.type, $p.elem, $p.struct, $p.words, $p.name, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&((function(){var _b=_i;return RETURNS($)&&_($)&&TypeDeclr($r)&&(function(){ addFuncTypeReturn($ftname, $r.type, $r.elem, $r.struct, $r.words, _s, _i); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($r)&&(function(){   /* PARKED for Impala 3.0 - see design/ParkedFeatures.md */ fail('Multiple return values are not supported in Impala 2.0', _s, _i, 'E428', 'declare a single return type for this funcptr type'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ endFuncType($ftname); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TypeDeclr($){var $base=createParserContext(),$desc,$id=createParserContext();return (function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = pointerDesc($desc); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&((function(){var _b=_i;return Identifier($id)&&(function(){ $.name = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ var td = descTypeElem($desc); if (td.type === 'S') { $.type = 'S'; $.struct = td.elem; $.elem = undefined; $.words = structWords(td.elem); } else { $.type = td.type; $.elem = td.elem; $.struct = undefined; $.words = (td.type === 't' ? 1 : undefined);   /* scalar funcptr: one word */ } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncDecl($){var $inl,$inlAt,$fnAt,$id=createParserContext(),$inp=createParserContext(),$out=createParserContext(),$v=createParserContext(),$,$loc=createParserContext();return (function(){var _b=_i;return (function(){ $inl = false; ; return true})()&&((function(){var _b=_i;return INLINE($)&&(function(){ $inl = true; $inlAt = _i; ; return true})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&FUNCTION($)&&_($)&&(function(){ $fnAt = _i; ; return true})()&&Identifier($id)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ if ($inl) {   /* PARKED for Impala 3.0 - see design/ParkedFeatures.md */ fail('Inline functions are not supported in Impala 2.0', _s, $inlAt, 'E439', 'an expansion needs GAZL 2 `SCOP` / `ENDS` to place its locals; ' + 'drop `inline` to compile it as an ordinary function'); } assert(validateStock('%')); assert(validateStock('<')); /* every compile-time scratch borrowed by the previous function/globals must be back in the pool at this clean boundary - catches offset-scratch leaks at the source */ assert(stock['<'].length === counters['<'], 'compile-time scratch leak before ' + $id._ + ': ' + (counters['<'] - stock['<'].length) + ' unreturned'); output(''); output(';-----------------------------------------------------------------------------'); /* declare the function symbol */ declare( undefined, 'functions', $id._, 'U', true, undefined, _s, $fnAt   /* the NAME, not the `(` this action already ate */ ); var entry = symbols.functions[$id._]; if (entry) { if (!entry.signature) { entry.signature = {}; } entry.signature.params = []; entry.signature.returns = '?'; entry.signature.returnElem = undefined; entry.signature.returnName = undefined; entry.signature.sourceCode = _s; entry.signature.sourceOffset = declOffset; entry.signature.sourceName = sourceName; entry.signature.returnResolved = false; entry.pendingReturnPlaceholder = undefined; entry.pendingReturnDeclaration = undefined; } ; return true})()&&ArgsDecl($inp)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){var _b=_i;return RETURNS($)&&_($)&&VarDecl($out)&&(function(){ var entry = symbols.functions[$id._]; if (entry) { entry.pendingReturns = undefined; /* `$out.at` is the NAME; `_i` here has already skipped past the whole declarator onto the next clause, which put E463's caret on `locals`. */ addReturn(entry, $out.name, $out.type, $out.elem, $out.size, _s, $out.at, $out.struct, $out.words); } ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){   /* PARKED for Impala 3.0 - see design/ParkedFeatures.md */ fail('Multiple return values are not supported in Impala 2.0', _s, _i, 'E428', 'return one value, or pass extra results back through pointer out-parameters'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ var entry = symbols.functions[$id._]; if (entry && entry.signature) { var rl = entry.pendingReturns; entry.signature.returnList = rl; entry.signature.returnCount = rl.length; entry.signature.returns = rl[0].type; entry.signature.returnElem = rl[0].elem; entry.signature.returnName = rl[0].rawName; entry.signature.returnStruct = rl[0].struct; var _rw = 0;                    /* total output-window words (struct returns span >1) */ for (var _wi = 0; _wi < rl.length; ++_wi) _rw += (rl[_wi].type === 'S' ? rl[_wi].words : 1); entry.signature.returnWords = _rw; resolveFunctionReturnType($id._, rl[0].type, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ /* implicit 1-word return: even void functions expose a single-word PARA so legacy call sites keep a deterministic return slot and the JSPEG output matches the historical PPEG layout. */ var entry = symbols.functions[$id._]; if (entry) { entry.pendingReturns = undefined; entry.pendingReturnPlaceholder = { sourceCode: _s, sourceOffset: _i }; } if (entry && entry.signature) { entry.signature.returns = 'V'; entry.signature.returnElem = undefined; entry.signature.returnName = undefined; entry.signature.returnCount = 0; entry.signature.returnList = []; resolveFunctionReturnType($id._, 'V', _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ /* declare input parameters */ var entry = symbols.functions[$id._]; if (entry && entry.signature) { entry.signature.params = copyParams($inp._, $inp.n); } emitFunctionSignature($id._, _s, _i); if (entry) { if (entry.pendingReturns && entry.pendingReturns.length > 0) { for (var _ri = 0; _ri < entry.pendingReturns.length; ++_ri) { var ret = entry.pendingReturns[_ri]; rejectByValueStruct(ret.type, ret.struct, ret.rawName, true, ret.sourceCode, ret.sourceOffset); { declare( 'OUT?', 'locals', ret.name, ret.type, false, ret.size, ret.sourceCode, ret.sourceOffset, undefined, ret.elem ); } } entry.pendingReturns = undefined; } else if (entry.pendingReturnPlaceholder) { var placeholder = entry.pendingReturnPlaceholder; declare( 'PARA', 'locals', undefined, '?', false, '*1', placeholder.sourceCode, placeholder.sourceOffset ); entry.pendingReturnPlaceholder = undefined; } } iterate($inp._, function (p) { rejectByValueStruct(p.type, p.struct, p.name, false, _s, p.at); { declare( 'INP?', 'locals', '$' + p.name, p.type, true, (p.size !== undefined ? '*' + p.size : undefined), _s, p.at, undefined, p.elem ); } }); ; return true})()&&((function(){var _b=_i;return LOCALS($)&&_($)&&LocalsDecl($loc)&&(function(){ iterate($loc._, function (v) { if (v.type === 'S') {         /* struct value local -> LOCA *sizeof, remember struct */ declare( 'LOCA', 'locals', '$' + v.name, 'S', false, structAllocSize(v.struct, _s, v.at), _s, v.at, undefined, structDesc(v.struct) ); } else { declare( 'LOC?', 'locals', '$' + v.name, v.type, false, (v.type === 'A' ? arrayAllocSize(v.elem, v.size, v.name, $id._, v.dims) : (v.words !== undefined ? '*' + v.words : undefined)), _s, v.at, undefined, v.elem ); } if (v.type === 'A') {   /* the ONLY place the owning function's name is in scope, so what a later subscript needs to bounds-check this array is recorded here - see arrayExtent's shape */ symbols.locals['$' + v.name].extent = arrayExtent(v.name, $id._, v.name, v.size, v.dims); } }); returnExtent('locals of ' + $id._, $loc._); /* the count and axis scratches ArrayDecl held for this clause (see there) */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ output(';-----------------------------------------------------------------------------'); ; return true})()&&Block($)&&(function(){ /* wrap-up body */ /* The closing RETU goes in BEFORE the pass, so the pass can see it: `goto out;` where `out:` is the end-of-body label is Impala's only early-exit idiom, and it lands exactly here. */ emit('--^', undefined, undefined, undefined, undefined); checkReturnAssigned(symbols.functions[$id._]); processBranches(); flushMetaCode('\t'); emittedGuards = {};   /* per function: labels are too */ prune(symbols.locals); labelCounter = 0; output(''); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ExternDecl($){var $at,$id=createParserContext(),$sname,$f=createParserContext(),$nmAt,$inp=createParserContext(),$out=createParserContext(),$a=createParserContext();return (function(){var _b=_i;return EXTERN($)&&_($)&&(function(){ $.scope = 'globals'; $.structFwd = false; $at = _i;   /* the declaration itself - end-of-rule positions have skipped past it */ pendingProto = undefined; ; return true})()&&(function(){var _b=_i;return STRUCT($)&&_($)&&Identifier($id)&&(function(){ $.structFwd = true; $sname = $id._; beginExternStruct($id._, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ /* a bodyless `extern struct G` never reaches endStruct, so only the braced form opens */ openStruct = $sname; ; return true})()&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return ArrayDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)||VarDecl($f)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ addStructField($sname, { name: $f.name, type: $f.type, elem: $f.elem, struct: $f.struct, size: $f.size, dims: $f.dims }, _s, _i); ; return true})()&&((function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){   /* Declare the host-owned interface so a signature-row consumer can check the layout the host supplies (.o.Name.field / .z.Name) against what Impala assumed, the way extern globals and function signatures are already checked. A re-declaration of a struct DEFINED here describes no host layout, so it publishes no row - the definition already published the real one. */ if (!endStruct($sname)) { emitStandaloneSignatureComment( structSignatureRow( $sname, true, sourceName, _s, declOffset)); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){var _b=_i;return FUNCTION($)&&(function(){ $.type  = 'U';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NATIVE($)&&(function(){ $.type  = 'N';  $.scope = 'functions'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&(function(){ $nmAt = _i; ; return true})()&&Identifier($id)&&(function(){ $.name  = $id._; $.at = $nmAt; ; return true})()&&((function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&ArgsDecl($inp)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){   /* Optional PROTOTYPE. Name-only stays valid and asserts nothing (a wildcard a consumer skips); a prototype is a checkable assertion, so calls get argument type-checking and the emitted signature row carries real types. */ pendingProto = { args: copyParams($inp._, $inp.n), ret: undefined }; ; return true})()&&((function(){var _b=_i;return RETURNS($)&&_($)&&TypeDeclr($out)&&(function(){   /* The name is OPTIONAL, as it already was for the sibling `functype ... returns V`: only type/elem/struct are read out of it, so `returns int` says everything `returns int n` did without making the author invent an identifier that is never printed nor referenced. TypeDeclr rather than a second rule for exactly that reason - the two declaration forms had no business differing. */ if (pendingProto) pendingProto.ret = { type: $out.type, elem: $out.elem, struct: $out.struct }; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&TypeDeclr($)&&(function(){   /* PARKED for Impala 3.0 - see design/ParkedFeatures.md */ fail('Multiple return values are not supported in Impala 2.0', _s, _i, 'E428', 'declare a single return value for this extern'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ externArray = true; ; return true})()&&ArrayDecl($a)&&(function(){ externArray = false; $.type = 'A'; $.name = $a.name; $.at   = $a.at; $.elem = $a.elem; $.dims = $a.dims; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ externArray = false; ; return true})()&&VarDecl($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ if ($.structFwd) { cancelStructRedeclaration($sname); return true; } declare( undefined,                 // no section for extern
                                                                             $.scope, $.name, $.type, false,                     // not readonly
                                                                             '?', _s, $.at, undefined, $.elem ); /* A host-owned SHAPE needs an extent record even though every value in it belongs to the host: `dims` names the axes so a subscript can stride by `.d.g.0` and defer its bounds against it, and its LENGTH is the rank a subscript is checked against. Only for a shape - a rank-1 `extern array` keeps carrying no extent, exactly as before, so its every path is unchanged. `declare` rebuilds the record, so this has to land after it. */ if ($.type === 'A' && $.dims !== undefined) { symbols.globals[$.name].extent = arrayExtent($.name, undefined, $.name, undefined, $.dims); } if ($.scope === 'functions') { var entry = symbols.functions[$.name]; var signature = entry && entry.signature; if (entry) { if (!signature) { signature = entry.signature = {}; } if (signature.sourceName === undefined) { signature.sourceName = sourceName; } if (signature.sourceCode === undefined) { signature.sourceCode = _s; signature.sourceOffset = declOffset; signature.sourceName = sourceName; } if (entry.kind !== 'FUNC') {  /* a definition here already resolved it - do not un-resolve it */ signature.returnResolved = false; } } var role = ($.type === 'N' ? 'extern native' : 'extern func'); var placeholderSignature = { params: [], returns: undefined, sourceName: sourceName, sourceCode: _s, sourceOffset: declOffset, }; var _proto = pendingProto; pendingProto = undefined; if (_proto !== undefined) {   /* a declared prototype: real params + at most one return */ for (var _pi = 0; _pi < _proto.args.length; ++_pi) { var _p = _proto.args[_pi]; rejectByValueStruct(_p.type, _p.struct, _p.name, false, _s, $at); } var _pp = copyParams(_proto.args, _proto.args.length); var _pr = _proto.ret; if (_pr !== undefined) rejectByValueStruct(_pr.type, _pr.struct, _pr.name, true, _s, $at); placeholderSignature.params      = _pp; placeholderSignature.returns     = (_pr !== undefined ? _pr.type : 'V'); placeholderSignature.returnElem  = (_pr !== undefined ? _pr.elem : undefined); placeholderSignature.returnCount = (_pr !== undefined ? 1 : 0); placeholderSignature.returnWords = (_pr !== undefined ? 1 : 0); placeholderSignature.returnResolved = true; if (entry) { var _defined = (entry.kind === 'FUNC'); checkExternAgreement($.name, placeholderSignature, (_defined ? entry.signature : entry.externProto), _defined, _s, $at); entry.externProto = placeholderSignature; if (!_defined) {               /* a definition here outranks it; otherwise publish it so call sites check against it */ entry.signature.params         = _pp; entry.signature.returns        = placeholderSignature.returns; entry.signature.returnElem     = placeholderSignature.returnElem; entry.signature.returnCount    = placeholderSignature.returnCount; entry.signature.returnWords    = placeholderSignature.returnWords; entry.signature.returnResolved = true; } } } emitStandaloneSignatureComment( formatFunctionSignatureComment( $.name, placeholderSignature, role, sourceName, _s, declOffset ) ); } else if ($.scope === 'globals') { emitStandaloneSignatureComment( formatGlobalSignatureComment( 'GLOB', $.name, $.type, $.size, 'extern', sourceName, _s, declOffset, $.elem ) ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ConstDecl($){var $base=createParserContext(),$desc,$nf,$t,$telem,$cStart,$id=createParserContext(),$cInitStart,$x=createParserContext();return (function(){var _b=_i;return CONST($)&&_($)&&TypeBase($base)&&(function(){ /* Same type grammar as every other declarator (TypeBase, not bare BASE_TYPE), so a const can name a struct pointer or a named functype - a const is an assembler-level address/scalar constant, and those two are just addresses. A struct VALUE is the one shape that has no scalar constant form. */ $desc = $base._; $nf = noForward; noForward = true; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = pointerDesc($desc); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ var cd = descTypeElem($desc); if (cd.type === 'S') { fail('A const cannot be a struct value - use a struct pointer (' + 'const ' + cd.elem + ' pointer)', _s, _i, 'E447'); } $t = cd.type; $telem = cd.elem;    /* 't' + name for a funcptr constant */ $cStart = _i;   /* `Identifier` eats trailing space, so _i would name the NEXT declaration (E453) */ ; return true})()&&Identifier($id)&&(function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){ $cInitStart = _i; ; return true})()&&Expr($x)&&(function(){ checkInitTarget($t, $telem, $x._, _s, $cInitStart); declare( '! DEF?', 'defines', $id._, $t, true, makeConstant($x._, $t, _s, $cInitStart), _s, $cStart, formatConstSignatureComment( $id._, $t, sourceName, _s, declOffset, $telem ), $telem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ /* `export` says "this unit provides it"; a valueless const says "someone else does". The row below publishes it as an extern either way, so the keyword was silently dropped. */ if (exportNext) { fail('`export` contradicts a valueless `const` - ' + $id._ + ' is provided elsewhere, not by this unit', _s, $cStart, 'E453', 'give it a value to export it, or drop `export`'); } declare( undefined, 'defines', $id._, $t, true, undefined, _s, $cStart, undefined, $telem ); emitStandaloneSignatureComment(  /* valueless -> host/runtime defines it: publish it as an extern so it links-checks */ formatConstSignatureComment( $id._, $t, sourceName, _s, declOffset, $telem, true ) ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ noForward = $nf; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function GlobalDecl($){var $section,$v=createParserContext(),$vStruct,$init,$initStart,$d=createParserContext(),$binit,$x=createParserContext(),$a=createParserContext(),$aStructEl,$shaped,$needNested;return (function(){var _b=_i;return (function(){var _b=_i;return GLOBAL($)&&(function(){ $section = 'GLOB'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||READONLY($)&&(function(){ $section = 'CNST'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||TEMPORARY($)&&(function(){ $section = 'TEMP'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&_($)&&(function(){var _b=_i;return VarDecl($v)&&(function(){ $vStruct = ($v.type === 'S'); if ($vStruct) {              /* struct value global -> one zeroed GLOB/CNST/TEMP *sizeof */ declare( $section, 'globals', $v.name, 'S', ($section === 'CNST'), structAllocSize($v.struct, _s, $v.at), _s, $v.at, formatGlobalSignatureComment( $section, $v.name, 'S', undefined, undefined, sourceName, _s, declOffset, $v.struct), structDesc($v.struct) ); } else { declare( $section, 'globals', undefined, $v.type, ($section === 'CNST'), '*1', _s, $v.at ); $init = ZEROES[$v.type]; } ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){ $initStart = _i; ; return true})()&&(function(){var _b=_i;return Braced($d)&&(function(){ if (!$vStruct) fail('Brace initializers are only for struct values', _s, $initStart, 'E422'); $binit = []; buildStructInit($v.struct, $d._, $binit, _s, $initStart); emitInitData($binit, _s, $initStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($x)&&(function(){ if ($vStruct)                    /* $initStart, not _i: `Expr` ate the trailing space too */ fail('A struct value needs a brace initializer', _s, $initStart, 'E421'); checkInitTarget($v.type, $v.elem, $x._, _s, $initStart); $init = makeConstant($x._, $v.type, _s, $initStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ if (!$vStruct) declare( 'DAT?', 'globals', $v.name, $v.type, ($section === 'CNST'), $init, _s, $v.at, formatGlobalSignatureComment( $section, $v.name, $v.type, undefined, undefined, sourceName, _s, declOffset, $v.elem ), $v.elem ); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($a)&&(function(){ declare( $section, 'globals', $a.name, 'A', ($section === 'CNST'), arrayAllocSize($a.elem, $a.size, $a.name, undefined, $a.dims), _s, $a.at, formatGlobalSignatureComment( $section, $a.name, 'A', $a.size, undefined, sourceName, _s, declOffset, $a.elem, $a.dims ), $a.elem ); symbols.globals[$a.name].extent = arrayExtent($a.name, undefined, $a.name, $a.size, $a.dims); returnExtent('global ' + $a.name,   /* declared: this consumer is done. Fields spelled out because bare `$a._` is the rule's return value, not its context - see declEntry */ [ { size: $a.size, dims: $a.dims } ]); $aStructEl = isStructAtom($a.elem); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){   /* the list is fully consumed before these checks run, so _i would land on the NEXT declaration - name the initializer itself */ $initStart = _i; /* Hand InitList the DECLARED element type. Without it every entry was checked against its own type, which no value can fail, so `int array A[2] = { 1, "s" }` stored a pointer in an int slot and `float array F[2] = { 1, 2 }` stored the INTEGER bit pattern and read back 1.4013e-45. The scalar paths have always been this strict (`global float f = 1` is E407); only the array path was not. A struct-element array keeps reporting the friendlier E422 below, so it states no target at all. */ initTarget = ($aStructEl ? undefined : $a.elem); /* Every multi-dim shape mirrors its axes in the braces (`[2, 3]` -> `{ {,,}, {,,} }`), so a shape mistake is caught rather than silently shifting every element; a struct-element array nests one group per element the same way. Both need nested braces (a plain 1-D array stays flat). A literal shape is checked here; a symbolic one is validated at GAZL assembly - see buildShapedInit. */ $shaped = ($a.dims !== undefined); $needNested = ($shaped || $aStructEl); ; return true})()&&(function(){var _b=_i;return InitList($d)&&(function(){   /* flat list -> 1-D scalar arrays and symbolic-axis shapes. `{ }` states nothing, so there is nothing to nest and nothing to place: it zero-fills, exactly as it already did for a struct value and a plain array. Rejecting it here asked for "one group per element" when no element had been given. */ if ($needNested && $d._ > 0) fail(($aStructEl ? 'A struct-element array needs nested braces, one group per element' : 'A shaped array needs nested braces, one group per axis'), _s, $initStart, 'E422'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Braced($d)&&(function(){   /* nested braces -> struct-element arrays and literal shapes */ if (!$needNested) fail('Nested brace initializers are for struct-element or shaped arrays', _s, $initStart, 'E422'); $binit = []; $binit.target = ($aStructEl ? undefined : $a.elem);   /* types the DATA rows; see emitInitData */ /* The same filler a struct FIELD uses. Nothing follows a standalone array inside its own allocation, so the gap it may report needs no blockInitFrom here. Fields spelled out because bare `$a._` is the rule's return value, not its context - see declEntry. */ fillArray({ name: $a.name, elem: $a.elem, size: $a.size, dims: $a.dims }, undefined, $d._, $binit, _s, $initStart); emitInitData($binit, _s, $initStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Braced($){var $i=createParserContext();return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return BracedEntry($i)&&(function(){ $._[$.n++] = $i._; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&BracedEntry($i)&&(function(){ $._[$.n++] = $i._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BracedEntry($){var $fname,$fat,$id=createParserContext(),$e=createParserContext();return (function(){var _b=_i;return (function(){ $fname = undefined; $fat = _i; ; return true})()&&((function(){var _b=_i;return Identifier($id)&&(_s[_i]===":")&&(++_i,true)&&_($)&&(function(){ $fname = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&BracedItem($e)&&(function(){ var _v = $e._;   /* bare `$e._` is the VALUE; `$e.field` would set a CONTEXT property, and `Braced` stores only the value */ _v.field = $fname; _v.at = $fat; $._ = _v; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BracedItem($){var $b=createParserContext(),$x=createParserContext();return (function(){var _b=_i;return Braced($b)&&(function(){ $._ = { braced: $b._ }; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($x)&&(function(){ var m = metaSlot($x._); var op = makeRValue(m, '#<&'); if (span(op[0] || '', '#<&') !== 1) fail('Initializer must be a constant', _s, _i, 'E407'); $._ = { op: holdConstant(op, m.type, _s, _i), type: m.type, elem: m.elem }; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function InitList($){var $d,$type,$n,$entryAt,$x=createParserContext(),$;return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&(function(){ $d = ' '; $type = undefined; $n = 0; ; return true})()&&((function(){var _b=_i;return (function(){var _l=_i,_x=(function(){var _b=_i;return Identifier($)&&(_s[_i]===":")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()&&(function(){ $entryAt = _i; ; return true})()&&Expr($x)&&(function(){ var first = initEntry(metaSlot($x._), _s, $entryAt); $type = first.type; $d += first.op; ++$n; ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){var _l=_i,_x=(function(){var _b=_i;return Identifier($)&&(_s[_i]===":")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()&&(function(){ $entryAt = _i; ; return true})()&&Expr($x)&&(function(){ var next = initEntry(metaSlot($x._), _s, $entryAt); var xType = next.type, constant = next.op; /* decide if we need to flush DATA */ if (  constant[0] === '<' || $d[1] === '<' || ($d + ' ' + constant).length >= 55) { declare( initElemRow(), 'globals', undefined, xType, true, $d.substr(1), _s, _i ); $d = ''; } $d += ' ' + constant; $type = xType; ++$n; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ $._ = $n;   /* entry count, set at the END: the `!(Identifier ':')` predicate compiles to `Identifier($)` and clobbers `$._` mid-rule */ if ($d.substr(1) !== '') { declare( initElemRow(), 'globals', undefined, $type, true, $d.substr(1), _s, _i ); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArgsDecl($){var $v=createParserContext();return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return VarDecl($v)&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size, $v.dims, $v.at); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&VarDecl($v)&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size, $v.dims, $v.at); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function LocalsDecl($){var $v=createParserContext();return (function(){var _b=_i;return (function(){ $._ = []; $.n = 0; ; return true})()&&((function(){var _b=_i;return (function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size, $v.dims, $v.at); ; return true})()&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){var _b=_i;return VarDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)||ArrayDecl($v)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ $._[$.n++] = declEntry($v.type, $v.elem, $v.struct, $v.words, $v.name, $v.size, $v.dims, $v.at); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function TypeBase($){var $t=createParserContext(),$id=createParserContext();return (function(){var _b=_i;return BASE_TYPE($t)&&_($)&&(function(){ $._ = CASTS_TO_TYPES[$t._]; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Identifier($id)&&(function(){ /* the ONE place a raw type name becomes a descriptor */ if (isStructName($id._)) $._ = structDesc($id._); else if (isFuncTypeName($id._)) $._ = funcTypeDesc($id._); else fail('Unknown type ' + $id._, _s, _i, 'E413'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function VarDecl($){var $base=createParserContext(),$desc,$nameStart,$id=createParserContext();return (function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = pointerDesc($desc); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ $nameStart = _i; ; return true})()&&Identifier($id)&&(function(){ checkReservedName($id._, 'variable', _s, $nameStart); var vd = descTypeElem($desc); if (vd.type === 'S') { $.type = 'S'; $.struct = vd.elem; $.elem = undefined; $.words = structWords(vd.elem); } else { $.type = vd.type;       /* 't' + the name for a named funcptr type */ $.elem = vd.elem; $.struct = undefined; $.words = undefined;    /* scalar: a single word, no size operand */ } $.name = $id._; $.at   = $nameStart;   /* the NAME - `Identifier` ate the trailing space, so an end-of-rule _i would name the NEXT declaration */ $.size = undefined; $.dims = undefined;    /* a recycled slot: a scalar after a SHAPED array would inherit its axes */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ArrayDecl($){var $desc,$extent,$dims,$base=createParserContext(),$nameStart,$id=createParserContext(),$extentStart,$x=createParserContext(),$y=createParserContext(),$size;return (function(){var _b=_i;return (function(){ $desc = undefined; $extent = undefined; $dims = undefined; ; return true})()&&((function(){var _b=_i;return TypeBase($base)&&(function(){ $desc = $base._; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $desc = pointerDesc($desc); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&ARRAY($)&&_($)&&(function(){ $nameStart = _i; ; return true})()&&Identifier($id)&&(function(){ checkReservedName($id._, 'array', _s, $nameStart); ; return true})()&&((function(){var _b=_i;return (_s[_i]==="[")&&(++_i,true)&&_($)&&(function(){ $extentStart = _i; $dims = []; ; return true})()&&(function(){var _b=_i;return Expr($x)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Expr($y)&&(function(){   /* an EXTRA axis. Held as its own constant; the axes stay separate so each can be checked against its own index, and only the PRODUCT becomes the allocation size. */ $dims.push(arrayAxis($y._, _s, $extentStart)); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ $size = makeConstant($x._, 'i', _s, $extentStart); /* A NEGATIVE extent runs the layout BACKWARDS. On a plain array the assembler catches it, because the count reaches it as a `GLOB *size` / `LOCA *size` operand it type-checks ("Incompatible types: .z.g"). A struct FIELD's count is only ever ADDED into the offset accumulator, which subtracts without complaint - `struct T { int a; int array b[-1]; int c }` compiles, assembles, runs, and puts `a` and `c` in the SAME WORD. Rejected here, at the declaration, because that is where the extent is still a number and where the invariant belongs. Zero is left alone: it wastes a field but aliases nothing, and every constant index into it is already out of range. */ if (constInt($size) < 0) fail('Array extent is negative: ' + dropHash($size), _s, $extentStart, 'E462', 'an array holds zero or more elements'); $extent = dropHash($size);   /* element count - may be a symbolic const */ if ($dims.length > 0) {              /* a SHAPE: allocate the product, keep the axes */ $dims.unshift($extent);          /* written order, outermost first */ $extent = axesProduct($dims); $dims.reverse();                 /* stride order: axis 0 is innermost */ } /* THE CONSUMER OWNS THE BORROW. A folded extent lives in a `<X>` scratch, and a scratch is recycled on the next borrow - so whoever still has to READ this one decides when it goes back: endStruct after the layout block, FuncDecl after the locals pass, GlobalDecl right after its declaration. Freeing it here instead let the NEXT declarator in the same list borrow the same scratch and overwrite the extent, which is how two array locals in one clause silently got whichever count was folded last. */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){ $dims.push(undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ $dims.push(undefined);   /* rank = commas + 1, so `[]` is rank 1 */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ /* A HOST-OWNED array states its RANK and no extent: the host supplies `.o.`, `.z.` and every `.d.`, so a number here would be an unverifiable claim Impala never reads. Rank is not optional with it, because rank is what a subscript is checked against and what a stride is read from - and it is the ONLY thing about the layout this side genuinely knows. */ var hostOwned = hostOwnedArray(); if (hostOwned && $extent !== undefined) { fail('A host-owned array must not state a size', _s, $extentStart, 'E430', 'the host owns this layout - state only the rank, `array ' + $id._ + '[' + new Array($dims.length).join(',') + ']`');   /* rank N reads as N-1 commas */ } if (hostOwned && $dims === undefined) { fail('Host-owned array ' + $id._ + ' must state its rank', _s, $nameStart, 'E432', 'write `array ' + $id._ + '[]` for one axis, `[,]` for two - ' + 'one comma per axis after the first'); } if (!hostOwned && $extent === undefined) { fail('Array ' + $id._ + ' needs a size', _s, $nameStart, 'E431', 'only a host-owned array - an `extern array` or an `extern ' + 'struct` field - states rank without extents'); } $.type = 'A'; $.elem = $desc; $.name = $id._; $.at   = $nameStart;                 /* see VarDecl */ $.size = $extent; $.words = $extent;                   /* an array's words ARE its count; a struct element scales symbolically, in arrayAllocSize */ $.dims = (($dims !== undefined && $dims.length > 1) ? $dims : undefined); /* only a SHAPE carries axes; a 1-D array has none, which is what keeps its every path unchanged */ ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Statement($){var $lblStart,$label=createParserContext();return (function(){var _b=_i;return (function(){ /* Scanned IN PLACE rather than copying the rest of the unit first. `cut >= 0` was dead: find returns 0..length and never negative. (Measured: the copy cost nothing - V8 slices rather than copies - so this is a dead-branch removal, NOT the perf fix it looks like.) */ var cut = find(_s, "{;\r\n", _i); var txt = _s.substring(_i, cut); emitMeta({ operator:';', type:undefined, operands:[ txt, undefined, undefined ] }); ; return true})()&&((function(){while((function(){var _b=_i;return (function(){ $lblStart = _i; ; return true})()&&Identifier($label)&&(_s[_i]===":")&&(++_i,true)&&_($)&&(function(){ emitMeta({ operator:'<--', type:undefined, operands:[ '@' + $label._, undefined, undefined ] }); /* carry the source position so processBranches can name the `.impala` line if this label is a duplicate */ var lbl = metacode[metacode.length - 1]; lbl.labelSource = _s; lbl.labelOffset = _i; /* the 1.x `goto break;` early-exit idiom is the --legacy case */ checkReservedName($label._, 'label', _s, $lblStart); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Assert($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Block($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Copy($)||(_im=(_i>_im?_i:_im),_i=_b,false)||DoWhile($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Loop($)||(_im=(_i>_im?_i:_im),_i=_b,false)||For($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Goto($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Return($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Break($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Continue($)||(_im=(_i>_im?_i:_im),_i=_b,false)||If($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Switch($)||(_im=(_i>_im?_i:_im),_i=_b,false)||While($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Destructure($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Expr($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ releaseMeta($._); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Expr($){var $r=createParserContext();return (function(){var _b=_i;return Bitwise($)&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($r)&&(function(){ if (!dry) assign($._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Bitwise($){var $first,$op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return (function(){ $first = undefined; ; return true})()&&AddSub($)&&((function(){while((function(){var _b=_i;return BITWISE_OP($op)&&_($)&&AddSub($r)&&(function(){ if (!dry) { if ($first === undefined) $first = $op._; else if ($first !== $op._) mixedBitwise($first, $op._, _s, _i); binaryOp($op._, $._, $r._, _s, _i); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if (!dry) stampBitwise($._, $first !== undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function AddSub($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return MulDiv($)&&((function(){while((function(){var _b=_i;return ADDSUB_OP($op)&&_($)&&MulDiv($r)&&(function(){ if (!dry) binaryOp($op._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function MulDiv($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return PrePost($)&&((function(){while((function(){var _b=_i;return MULDIV_OP($op)&&_($)&&PrePost($r)&&(function(){ if (!dry) mulDivOp($op._, $._, $r._, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function PrePost($){var $op=createParserContext(),$cdesc,$ccast,$sid=createParserContext(),$pdepth;return (function(){var _b=_i;return (function(){var _b=_i;return PREFIX_OP($op)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&BASE_TYPE($op)&&_($)&&(function(){ $cdesc = CASTS_TO_TYPES[$op._]; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $cdesc = pointerDesc($cdesc); $ccast = 'pointer'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="(")&&(++_i,true)&&_($)&&Identifier($sid)&&(function(){ $pdepth = 0; ; return true})()&&((function(){while((function(){var _b=_i;return POINTER($)&&_($)&&(function(){ $pdepth = $pdepth + 1; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ /* With no `pointer` this is only a cast when the name is a FUNCTYPE, which is already a pointer and so needs no modifier - a struct has no by-value cast, and anything else is a plain parenthesized expression. Backing out (rather than failing) lets Value parse `(x) + 1`; the test above is a pure lookup, so there is no side effect to undo. */ if ($pdepth === 0) { if (!isFuncTypeName($sid._)) return false; $cdesc = funcTypeDesc($sid._); $ccast = 'funcptr'; } else { if (!isStructName($sid._) && !isFuncTypeName($sid._)) fail('Unknown type ' + $sid._, _s, _i, 'E413'); $cdesc = (isStructName($sid._) ? structDesc($sid._) : funcTypeDesc($sid._)); for (var _pk = 0; _pk < $pdepth; ++_pk) $cdesc = pointerDesc($cdesc); $ccast = 'pointer'; } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&PrePost($)&&(function(){ if (!dry) { if ($ccast) {          /* a named/pointer cast; a bare BASE_TYPE cast has no $ccast */ if ($ccast === 'funcptr') { checkFuncTypeCast($._, descTail($cdesc), _s, _i); } unaryOp($ccast, $._, _s, _i); setElem($._, descTail($cdesc)); } else { unaryOp($op._, $._, _s, _i); } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Value($)&&((function(){while((function(){var _b=_i;return FuncCall($)||(_im=(_i>_im?_i:_im),_i=_b,false)||Subscript($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FieldAccess($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Subscript($){var $idxAt,$subExtra,$subAt,$s=createParserContext(),$axisAt,$x=createParserContext();return (function(){var _b=_i;return (_s[_i]==="[")&&(++_i,true)&&_($)&&(function(){ $idxAt = _i; $subExtra = []; $subAt = []; ; return true})()&&Expr($s)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&(function(){ $axisAt = _i; ; return true})()&&Expr($x)&&(function(){ if (!dry) { $subExtra.push(makeRValue(metaSlot($x._))); $subAt.push($axisAt);   /* each axis carries its OWN position, or every per-axis diagnostic points at the first index */ } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="]")&&(++_i,true)&&_($)&&(function(){ if (!dry) { var sb = metaSlot($._); var axisOob = []; checkRank(sb, $subExtra.length + 1, _s, $idxAt); /* ONE index takes the path it took before this rule learned to count - `$s._` is handed on untouched, so a 1-D subscript is byte-identical. */ var sIdx = ($subExtra.length === 0 ? $s._ : foldAxes(sb, $s._, $subExtra, $subAt, axisOob, _s, $idxAt)); if ((sb.place && sb.arrayOf) || (sb.type === 'p' && isStructAtom(sb.elem))) subscriptStruct($._, sIdx, _s, $idxAt); else binaryOp('=[]', $._, sIdx, _s, $idxAt); /* AFTER the lowering, which is the terminal call that assigns `oobIndex`. Axis findings go FIRST: an E461 that can name the axis is the one that should fire, the flat check having nothing to say about `cells[0, 5]`. */ if (axisOob.length > 0) { var sx = metaSlot($._); sx.oobIndex = axisOob.concat(sx.oobIndex === undefined ? [] : sx.oobIndex); } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FieldAccess($){var $f=createParserContext();return (function(){var _b=_i;return (_s.substr(_i,2)==="->")&&(_i+=2,true)&&_($)&&Identifier($f)&&(function(){ if (!dry) fieldAccess($._, $f._, true, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]===".")&&(++_i,true)&&_($)&&Identifier($f)&&(function(){ if (!dry) fieldAccess($._, $f._, false, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FuncCall($){var $type,$;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ if (!dry) { $.count = 0; /* how many leading output slots the callee expects (>1 = multi-return) */ var _c = metaSlot($._); var _rs = 1; if (_c.operator === ':=' && _c.operands[1] && (_c.operands[1][0] === '&' || _c.operands[1][0] === '^')) { var _e = symbols.functions[_c.operands[1].substr(1)]; if (_e && _e.signature && _e.signature.returnWords !== undefined && _e.signature.returnWords > 1) _rs = _e.signature.returnWords;   /* multi-scalar OR by-value struct return window */ } else if (_c.type === 't' && isFuncTypeName(_c.elem)) { var _ft = functypes[_c.elem];   /* indirect call through a named funcptr type */ if (_ft.returnWords > 1) _rs = _ft.returnWords; } $.retSlots = _rs; $.words = 0;                            /* input words placed so far (struct args span >1) */ $.base  = borrowForCall(); for (var _os = 1; _os < _rs; ++_os)      /* reserve the extra output slots */ claimSlot($.base + _os); $.types = []; $.elems = []; $.opnds = []; $.svals = [];       /* by-value struct args, checked at the close */ } ; return true})()&&((function(){var _b=_i;return Argument($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&Argument($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){var _b=_i;return (_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){   /* a '(' after a value is always a call, so no valid parse ever backtracks out of one - and the prologue above already borrowed the call window, which a backtrack would leak into whatever diagnostic comes next. Reject here, where the syntax broke. */ if (!dry) fail('Malformed argument list', _s, _i, 'E442', 'expected , or ) here - and note that a comparison or a && / || group is not a value in Impala'); ; return true})()&&(function(){var _l=_i,_x=_($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){ if (!dry) { var callee = metaSlot($._); var callResultType = '?'; var signature = null; var calleeName = null; if (span(callee.type, 'tN') !== 1) { typeError( 'Invalid type for function call ({$type1})', _s, _i, callee.type , undefined, 'E408'); } if (callee.operator === ':=' && callee.operands[1] && (callee.operands[1][0] === '&' || callee.operands[1][0] === '^')) { calleeName = callee.operands[1].substr(1); var entry = symbols.functions[calleeName]; /* an Impala-defined function, or an extern with a DECLARED prototype (name-only externs carry no `params` and stay unchecked - they assert nothing) */ if (entry && entry.signature && (entry.kind === 'FUNC' || entry.signature.params)) { signature = entry.signature; } } else if (callee.type === 't' && isFuncTypeName(callee.elem)) { signature = functypes[callee.elem];   /* indirect call: check against the funcptr type */ } if (signature) { var params = signature.params || []; var actualCount = ($.types ? $.types.length : 0); var expectedCount = params.length; var label = (calleeName || 'function'); if (actualCount !== expectedCount) { fail( 'Invalid argument count when calling ' + label + ' (expected ' + expectedCount + ', got ' + actualCount + ')', _s, _i , 'E405'); } for (var argIdx = 0; argIdx < expectedCount; ++argIdx) { var expected = params[argIdx].type; var actual = $.types[argIdx]; if (actual === undefined) { actual = '?'; } if (actual === '?' || expected === undefined) { continue; } if (actual !== expected) { /* Name the struct when the actual is a struct VALUE, and point at `&`: passing `v` where `V pointer` is wanted is the common slip now that by-value struct params are parked for Impala 3.0. */ var _actualText = (isStructAtom($.elems && $.elems[argIdx]) ? 'struct ' + descName($.elems[argIdx]) : '{$type1}'); typeError( 'Argument type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (' + _actualText + ' vs expected {$type2})', _s, _i, actual, expected , 'E406', ((actual === 'S' && expected === 'p') ? 'pass its address with & (by-value struct params are parked for Impala 3.0)' : undefined)); } if (expected === 'S' && params[argIdx].struct !== undefined && $.elems[argIdx] !== params[argIdx].struct) { fail('Struct type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (expected ' + params[argIdx].struct + ', got ' + ($.elems[argIdx] || 'a non-struct value') + ')', _s, _i, 'E421'); } var expectedElem = params[argIdx].elem;   /* typed pointer param: assume loudly */ if (expected === 'p' && expectedElem !== undefined && $.opnds[argIdx] !== '&NULL' && $.elems[argIdx] !== expectedElem) { fail('Pointer element type mismatch for argument ' + (argIdx + 1) + ' when calling ' + label + ' (expected ' + elemVerbose(expectedElem) + ' elements, got ' + elemVerbose($.elems[argIdx]) + ' elements)', _s, _i, 'E202', 'use a cast: (' + elemVerbose(expectedElem) + ' pointer)'); } /* Same rule for a named funcptr param as for assignment, so the same check: `expected` is already 't' here, which is what the assign path derives from the r-value's own type. */ if (expected === 't' && isFuncTypeName(expectedElem)) { checkFuncPtrTarget(expectedElem, $.opnds[argIdx], 't', $.elems[argIdx], ' for argument ' + (argIdx + 1) + ' when calling ' + label, _s, _i); } } if (signature.returnResolved && signature.returns !== undefined) { callResultType = signature.returns; } else if (signature.expectedReturn !== undefined) { callResultType = signature.expectedReturn; } else if (signature.returns !== undefined) { callResultType = signature.returns; } } /* THE LAST DOOR for a by-value struct. Every declarator is guarded, but a name-only `extern function f` / `extern native f` has no parameter list to guard, so the parked by-value path ran unopposed at the call and baked a COPY size for a struct whose size Impala may not know - `*undefined` operands and a `*NaN` call window reached the artifact. Deliberately runs AFTER the signature loop above: a PROTOTYPED callee wanting a pointer gets the sharper "struct V vs expected pointer" instead of this. */ for (var _sv = 0; _sv < $.svals.length; ++_sv) { rejectByValueStruct('S', $.svals[_sv].struct, $.svals[_sv].name, false, _s, $.svals[_sv].at, true); } /* Built once: this is both the argument to the row below and the record a later refresh replays it from. The slices matter - `$.types`/`$.elems` are pooled and get reused. */ var callArgs = { name: calleeName, signature: signature, actualTypes: ($.types ? $.types.slice() : undefined), actualElems: ($.elems ? $.elems.slice() : undefined), sourceName: sourceName, sourceCode: _s, sourceOffset: _i           /* the CALL SITE, not the enclosing declaration */ }; var callComment = formatCallExpectationComment(callArgs, callResultType); var commentIndex = -1; if (callComment) { commentIndex = metacode.length; emit(';', undefined, callComment, undefined, undefined); commentIndex = metacode.length - 1; } var func = makeRValue(callee, '&^$%'); emit('()', '?', func, '%' + $.base, '*' + ($.words + $.retSlots)); returnBack(func); while ($.words-- > 0) {              /* free the argument words (past the output slots) */ returnBack('%' + ($.base + $.retSlots + $.words)); } makeMeta(callee, ':=', callResultType, undefined, '%' + $.base, undefined); /* Keep the RETURN's element type: `returns V pointer` must yield a V-pointer, not a bare one, or `*f()` cannot be recognised as a struct and typed-pointer assignment checks go blind. A funcptr type carries returnElem too, so indirect calls work. */ setElem(callee, signature ? signature.returnElem : undefined); /* A by-value struct return placed over the output window, and the multi-return window for destructuring, both lived here. Neither guard can be true in 2.0 - E427 rejects a struct return and E428 a second return value, both at the DECLARATOR - so no call ever reached them. Removed 2026-08-07, which is what design/ParkedFeatures.md had already claimed. `retSlots` itself stays: it is live, sizing the frame and the argument window. */ if (calleeName) { callee.callInfo = { name: calleeName, commentIndex: commentIndex, commentArgs: callArgs }; } else if (callee.callInfo) { callee.callInfo = undefined; } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Argument($){var $argAt,$a=createParserContext(),$type,$;return (function(){var _b=_i;return (function(){ $argAt = _i;   /* the argument itself; end-of-rule has skipped past it */ ; return true})()&&Expr($a)&&(function(){ if (!dry) { ++$.count; var meta = metaSlot($a._); checkIndexUse(meta);   /* a bare arg is placed without makeRValue */ if (meta.type === 'V') { typeError( 'Invalid type ({$type1})', _s, _i, meta.type, undefined, 'E406', 'a function with no `returns` clause produces no value' ); } if ($.types) { $.types.push(meta.type); } if ($.elems) {                       /* element chain + null-ness, captured */ $.elems.push(meta.elem);         /* before makeArgValue mutates the meta */ $.opnds.push(bareOperand(meta));  /* `&NULL` marks a null/nullfunc literal */ } var winSlot = $.base + $.retSlots + $.words; if (meta.type === 'S') {              /* by-value struct argument spans sizeof words */ /* Remember it for the LAST-door check at the close of the call. Not rejected here: a PROTOTYPED callee has a sharper message ("struct V vs expected pointer"), and its signature is only resolved once the argument list is complete. */ $.svals.push({ at: $argAt, struct: meta.struct, name: (typeof meta.base === 'string' && meta.base.charAt(0) === '$' ? meta.base.substr(1) : undefined) }); var w = structWords(meta.struct); copyStructArg($a._, winSlot, w); $.words += w; } else { makeArgValue($a._, winSlot); $.words += 1; } } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Group($){return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if (!dry) stampBitwise($._, false); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function BoolGroup($){var $label;return (function(){var _b=_i;return (_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ $label = undefined; ; return true})()&&And($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="||")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('t'); } emit('?->', true, $label, undefined, undefined); ; return true})()&&And($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ if ($label !== undefined) { emit('<-?', true, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function And($){var $label;return (function(){var _b=_i;return (function(){ $label = undefined; ; return true})()&&Comp($)&&((function(){while((function(){var _b=_i;return (_s.substr(_i,2)==="&&")&&(_i+=2,true)&&_($)&&(function(){ if ($label === undefined) { $label = newLabel('f'); } emit('?->', false, $label, undefined, undefined); ; return true})()&&Comp($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(function(){ if ($label !== undefined) { emit('<-?', false, $label, undefined, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Comp($){var $op=createParserContext(),$r=createParserContext();return (function(){var _b=_i;return (_s[_i]==="!")&&(++_i,true)&&_($)&&(function(){ /* `!` sits BELOW comparison, so `!x == 2` means `!(x == 2)` - the opposite of the C reading. Require its operand to be parenthesised (or another `!`); reuse the strict-expression machinery so `--legacy` keeps the old meaning with a warning. */ var c = _s.charAt(_i); if (c !== '!' && c !== '(') strictError( "'!' binds below comparison; parenthesise its operand", _s, _i, 'E103', 'write !(a == b), or to negate a value use (a) != b'); ; return true})()&&Comp($)&&(function(){ emit('!', undefined, undefined, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = true; ; return true})()&&(function(){var _l=_i,_x=Group($);_i=_l;return !_x})()&&(function(){ dry = false; ; return true})()&&BoolGroup($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ dry = false; ; return true})()&&Expr($)&&COMP_OP($op)&&_($)&&Expr($r)&&(function(){ checkCompMix($._, $r._, _s, _i); binaryOp($op._, $._, $r._, _s, _i); emitMeta($._); releaseMeta($._); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Assert($){var $okLabel,$start,$exprText,$str=createParserContext(),$;return (function(){var _b=_i;return ASSERT($)&&_($)&&(function(){ $okLabel  = beginDebugGuard('a'); $start    = _i; $exprText = ''; ; return true})()&&BoolGroup($str)&&(function(){   /* the assert's own source text becomes its message */ $exprText = _s.substring($start, _i); $exprText = $exprText.replace(/[ \t\r\n]+$/, ''); ; return true})()&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ endDebugGuard($okLabel, $str._, $exprText, _s, _i); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Block($){return (function(){var _b=_i;return (_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while(Statement($));})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Goto($){var $gotoStart,$label=createParserContext();return (function(){var _b=_i;return GOTO($)&&_($)&&(function(){ $gotoStart = _i; ; return true})()&&Identifier($label)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ emit('-->', undefined, '@' + $label._, undefined, undefined); /* carry the source position so processBranches can name the `.impala` line if this label is never defined (E445) */ var g = metacode[metacode.length - 1]; g.gotoSource = _s; g.gotoOffset = $gotoStart; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Return($){return (function(){var _b=_i;return RETURN($)&&_($)&&(function(){var _b=_i;return (_s[_i]===";")&&(++_i,true)&&_($)&&(function(){   /* bare `return;` is an early exit - RETU returns whatever the slot holds */ emit('--^', undefined, undefined, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){   /* `return expr;` - Impala returns via the named return variable. The failed `';'_` reset the cursor, so _i is back at the value. */ fail('return does not take a value - assign to the return variable instead', _s, _i, 'E448', 'a `returns T r` function returns whatever `r` holds; write `r = expr;` then `return;`'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Break($){var $start;return (function(){var _b=_i;return (function(){ $start = _i; ; return true})()&&(_s.substr(_i,5)==="break")&&(_i+=5,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()&&_($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ fail("'break' is not supported: a switch arm already does not fall through, and a loop is left with `goto` to a label after it", _s, $start, 'E450', 'replace `break;` with `goto` to a label placed after the loop'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Continue($){var $start;return (function(){var _b=_i;return (function(){ $start = _i; ; return true})()&&(_s.substr(_i,8)==="continue")&&(_i+=8,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()&&_($)&&(_s[_i]===";")&&(++_i,true)&&_($)&&(function(){ fail("'continue' is not supported: jump to a label at the end of the loop body with `goto`", _s, $start, 'E450', 'replace `continue;` with `goto` to a label at the loop-body end'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function If($){var $dontLabel,$,$doneLabel;return (function(){var _b=_i;return IF($)&&_($)&&BoolGroup($)&&(function(){ $dontLabel = newLabel('f'); emit('?->', false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){   /* `if (c) { } ; else` - the `;` is a complete empty statement, so the `if` ends there and `else` is left dangling. That fell through to a bare E001 pointing PAST the `else`; name the real culprit. Guard on the `;` first so the window scan is not per-statement. */ if (_s.charAt(_i) === ';' && /^;[ \t\r\n]*else(?![A-Za-z_$0-9])/.test(_s.substr(_i, 32))) { fail('a `;` here ends the `if`, leaving the `else` with nothing to attach to', _s, _i, 'E451', 'remove the `;` before `else` - a block or statement body takes no extra terminator'); } ; return true})()&&(function(){var _b=_i;return ELSE($)&&_($)&&(function(){ $doneLabel = newLabel('e'); emit('-->', undefined, $doneLabel, undefined, undefined); emit('<-?',  false, $dontLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('<--', undefined, $doneLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(function(){ emit('<-?', false, $dontLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DoWhile($){var $loopLabel;return (function(){var _b=_i;return DO($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<-?', false, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&WHILE($)&&_($)&&BoolGroup($)&&(function(){ emit('?->', true, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Loop($){var $loopLabel;return (function(){var _b=_i;return LOOP($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function For($){var $varStart,$var=createParserContext(),$gotInit,$init=createParserContext(),$toExpr=createParserContext(),$type,$to,$noLoopLabel,$loopLabel,$body=createParserContext();return (function(){var _b=_i;return FOR($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&(function(){ $varStart = _i; ; return true})()&&Variable($var)&&(function(){ /* loop-variable must be local, modifiable int / pointer */ var varMeta = metaSlot($var._); if (varMeta.operator !== '=' || span(varMeta.type, "ip") === 0) { fail( 'For variable must be a local modifiable int or pointer variable', _s, $varStart , 'E305', 'a parameter, global or non-scalar cannot be the loop variable - copy it into a `locals` int or pointer and loop over that'); } /* `FORp` steps exactly one WORD and is already a 3-operand form, so a struct pointer has nowhere to put its stride. Scaling only the bound silently ran sizeof(S) times too many (F2 in design/impala/Impala2Review.md). */ if (strideStruct(varMeta.elem) !== undefined) { fail( 'For variable must not be a struct pointer', _s, _i, 'E309', 'FORp cannot stride by a struct - use `while (p < end) { ...; p = &p[1]; }`'); } $gotInit = false;            /* flag to detect an explicit start value */ ; return true})()&&((function(){var _b=_i;return (_s[_i]==="=")&&(++_i,true)&&_($)&&Expr($init)&&(function(){ assign($init._, $var._, $init._, _s, _i); $gotInit = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&TO($)&&_($)&&Expr($toExpr)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var varMeta  = metaSlot($var._); var toMeta   = metaSlot($toExpr._); if (toMeta.type !== varMeta.type) { typeError( 'Incompatible types ({$type1} and {$type2})', _s, _i, varMeta.type, toMeta.type , 'E301'); } /* constant upper bound */ $to = makeRValue(toMeta); /* initial comparison  (var < to)                         */ emit( '<', toMeta.type, undefined, $gotInit ? metaSlot($init._).operands[1]     /* start value from “var = expr” */ : varMeta.operands[1],            /* or the original variable */ $to ); if ($gotInit) { releaseMeta($init._); } /* branch-out   and  loop label */ $noLoopLabel = newLabel('e'); emit('?->', false, $noLoopLabel, undefined, undefined); $loopLabel   = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&Statement($body)&&(function(){ var varMeta = metaSlot($var._); /* increment + jump back. A struct pointer never reaches here - E309 above rejects it, because `FORp` is already a 3-operand form with nowhere to put a stride (F2 in design/impala/Impala2Review.md). */ emit( '...', varMeta.type, varMeta.operands[1],        /* address of loop variable */ $to, $loopLabel ); emit('<-?', false, $noLoopLabel, undefined, undefined); returnBack($to); releaseMeta(varMeta); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Copy($){var $l=createParserContext(),$f=createParserContext(),$t=createParserContext(),$length,$type,$;return (function(){var _b=_i;return COPY($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($l)&&FROM($)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(_s[_i]===")")&&(++_i,true)&&_($)&&(function(){ var fromMeta = metaSlot($f._); var toMeta   = metaSlot($t._); $length = makeConstant($l._, 'i', _s, _i); var lengthHash = dropHash($length); if (fromMeta.type + toMeta.type !== 'pp') { returnBack($length); typeError( 'Invalid types ({$type1} and {$type2})', _s, _i, fromMeta.type, toMeta.type , 'E301'); } /* Both are pointers; ask what they point AT - but only when BOTH sides know, because `copy` flags a CONTRADICTION, not a broken promise. An assignment rejects untyped -> typed (E201): the variable must keep that promise for every later deref. A copy consumes both addresses on the spot, so an untyped source claims nothing to break - and reading an Impala 1 `array` blob into typed storage is the 1.0 idiom, not a defect. The LENGTH stays unchecked on purpose: a pointer has no extent. */ if (toMeta.elem !== undefined && fromMeta.elem !== undefined) { checkPtrAssign(toMeta, fromMeta, _s, _i); } var copyMeta = metaSlot($l._); makeMeta( copyMeta, 'copy', '?', makeRValue(toMeta, '&$%'), makeRValue(fromMeta, '&$%'), '*' + lengthHash ); emitMeta(copyMeta); returnBack($length); releaseMeta(copyMeta); ; return true})()&&(_s[_i]===";")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Destructure($){return (function(){var _b=_i;return DestTarget($)&&((function(){for(var _n=0;(function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&DestTarget($)||(_im=(_i>_im?_i:_im),_i=_b,false)})();++_n);return _n>0})())&&(_s[_i]==="=")&&(++_i,true)&&_($)&&(function(){   /* PARKED for Impala 3.0 - see design/ParkedFeatures.md. Kept only to recognise the shape and reject it well; the targets themselves are never needed. */ fail('Destructuring assignment is not supported in Impala 2.0', _s, _i, 'E429', 'assign one value per statement'); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DestTarget($){var $tgtGlobal,$id=createParserContext(),$tgtName;return (function(){var _b=_i;return (function(){ $tgtGlobal = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $tgtGlobal = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&Identifier($id)&&(function(){ $tgtName = $id._; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Switch($){var $f=createParserContext(),$t=createParserContext(),$size,$switcher,$,$switchExit,$progress,$stmt=createParserContext();return (function(){var _b=_i;return SWITCH($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&Expr($)&&(_s.substr(_i,2)==="==")&&(_i+=2,true)&&_($)&&Expr($f)&&TO($)&&_($)&&Expr($t)&&(function(){ var switchMeta = metaSlot($._); /* the switch expression must be an int */ if (switchMeta.type !== 'i') { fail('Switch expression needs to be int', _s, _i, 'E306'); } /* lower bound (compile-time constant) */ switchMeta.from = makeConstant($f._, 'i', _s, _i); /*    size = to - from   */ $size = subConstInt( makeConstant($t._, 'i', _s, _i), switchMeta.from ); /*   switcher = (expr − from)   */ $switcher = subConstInt( makeRValue(switchMeta, '$%'), switchMeta.from ); /* snapshot the range as plain numbers now: the operands are handed back to the scratch pool below, and a case is only checkable while both ends are known. */ switchMeta.fromNum = constInt(switchMeta.from); switchMeta.sizeNum = constInt($size); switchMeta.caseSeen = {}; switchMeta.switchLabel = newLabel('s'); $switchExit              = newLabel('e'); switchStack.push(switchMeta); emit( '-->#', switchMeta.type, $switcher, '*' + dropHash($size), switchMeta.switchLabel ); returnBack($switcher); returnBack($size); $progress = undefined;       /* track case / default presence */ ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)&&(_s[_i]==="{")&&(++_i,true)&&_($)&&((function(){while((function(){var _b=_i;return (function(){var _b=_i;return CASE($)&&_($)&&(function(){ /* multiple CASE groups -> fall-through handled here */ if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } else { $progress = 'gotCases'; } /* dump the literal “case ...” comment */ var snippet = _s.substring(_i, find(_s, ":\r\n", _i)); emit( ';', undefined, 'case ' + snippet, undefined, undefined ); ; return true})()&&CaseExpr($)&&((function(){while((function(){var _b=_i;return (_s[_i]===",")&&(++_i,true)&&_($)&&CaseExpr($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DEFAULT($)&&_($)&&(function(){ if ($progress === 'gotDefault') { fail('Default case already defined', _s, _i, 'E409', 'a switch has at most one `default` arm'); } else if ($progress !== undefined) { emit('-->', undefined, $switchExit, undefined, undefined); } var ctx = switchStack[switchStack.length - 1]; emit(';',    undefined, 'default',       undefined, undefined); emit('<--',  undefined, ctx.switchLabel,  undefined, undefined); $progress = 'gotDefault'; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(_s[_i]===":")&&(++_i,true)&&_($)&&Statement($stmt)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="}")&&(++_i,true)&&_($)&&(function(){ var ctx = switchStack.pop() || metaSlot($._); /* no explicit “default” -> hook it up now                        */ if ($progress !== 'gotDefault') { emit('<--', undefined, ctx.switchLabel, undefined, undefined); } emit('<--', undefined, $switchExit, undefined, undefined); returnBack(ctx.from); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function CaseExpr($){var $n;return (function(){var _b=_i;return Expr($)&&(function(){ /* offset = constant(expr) - switch.from                         */ var ctx      = switchStack[switchStack.length - 1]; var caseMeta = metaSlot($._); var baseFrom = (ctx ? ctx.from : caseMeta.from); var baseLabel = (ctx ? ctx.switchLabel : caseMeta.switchLabel); var caseConst = makeConstant(caseMeta, 'i', _s, _i); checkCaseValue(ctx, constInt(caseConst), _s, _i); $n = subConstInt(caseConst, baseFrom); /* A host may narrow the window from BELOW, which makes this arm UNREACHABLE, not wrong - the same situation as a case above the window, which `SWCH` already ignores harmlessly because the offset is only a table index it never looks up. Here the offset is pasted into the LABEL TEXT, so a negative one rendered `.s0.-4` and killed the whole module at load: one direction fell dead, the other refused to build. Guard the label with an assemble-time skip so both drop out of the configuration alike. A skipped line is abandoned BEFORE the label is interpreted (`skipUntilLabel`, GAZL.cpp), so `.s0.-4` is never constructed rather than constructed and rejected. Symbolic ranges only: with a literal range Impala knows the answer and E444 says so at Impala compile time, which is strictly better. */ var caseGuard; if (ctx !== undefined && ctx.fromNum === undefined) { caseGuard = newLabel('g'); emit('<> <', 'i', $n, '#0', caseGuard); } /* create label for this case                                     */ emit( '<--', undefined, baseLabel + '#' + dropHash($n), undefined, undefined ); if (caseGuard !== undefined) { emit('<-?', true, caseGuard, undefined, undefined); metacode[metacode.length - 1].mayRide = true; } returnBack($n); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function While($){var $loopLabel,$exitLabel;return (function(){var _b=_i;return WHILE($)&&_($)&&(function(){ $loopLabel = newLabel('l'); emit('<--', undefined, $loopLabel, undefined, undefined); ; return true})()&&BoolGroup($)&&(function(){ $exitLabel = newLabel('e'); emit('?->', false, $exitLabel, undefined, undefined); ; return true})()&&Statement($)&&(function(){ emit('-->', undefined, $loopLabel, undefined, undefined); emit('<-?', false, $exitLabel, undefined, undefined); ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Value($){var $base=createParserContext(),$f=createParserContext(),$i=createParserContext(),$s=createParserContext();return (function(){var _b=_i;return Group($)||(_im=(_i>_im?_i:_im),_i=_b,false)||SIZEOF($)&&_($)&&(_s[_i]==="(")&&(++_i,true)&&_($)&&TypeBase($base)&&(function(){ if (!dry) { var head = strideStruct($base._); if (head !== undefined) {            /* struct size -> symbolic .z.Name */ if (!isExternStruct(head) && !structDefined(head)) fail('sizeof of incomplete struct ' + head, _s, _i, 'E419'); makeMeta($._, ':=', 'i', undefined, '#' + extentSymbol(head), undefined); setElem($._, undefined); } else { makeMeta($._, ':=', 'i', undefined, '#1', undefined); setElem($._, undefined); } } ; return true})()&&(_s[_i]===")")&&(++_i,true)&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)||FloatLiteral($f)&&(function(){ if (!dry) { makeMeta($._, ':=', 'f', undefined, '#' + $f._, undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||IntegerLiteral($i)&&(function(){ if (!dry) { makeMeta($._, ':=', 'i', undefined, '#' + $i._, undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||StringLiteral($s)&&(function(){ if (!dry) { makeString('s', $._, evaluate($s._), _s, _i); setElem($._, 'i');      /* string data is int words (Impala 2) */ /* string data lives in a readonly section, so `"abc"[0] = 1` used to compile and fail at GAZL load - mark it readonly so the E404 element-write check catches the store at the source line */ metaSlot($._).readonly = true; } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULL($)&&_($)&&(function(){ if (!dry) { makeMeta($._, ':=', 'p', undefined, '&NULL', undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||NULLFUNC($)&&_($)&&(function(){ if (!dry) { makeMeta($._, ':=', 't', undefined, '&NULL', undefined); setElem($._, undefined); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)||Variable($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Variable($){var $global,$idStart,$id=createParserContext();return (function(){var _b=_i;return (function(){ $global = false; ; return true})()&&((function(){var _b=_i;return GLOBAL($)&&_($)&&(function(){ $global = true; ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)&&(function(){ $idStart = _i; ; return true})()&&Identifier($id)&&(function(){ if (!dry) { lookup($._, $id._, $global, _s, $idStart); } ; return true})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function Identifier($){return (function(){var _b=_i;return (function(){var _l=_i,_x=KEYWORD($);_i=_l;return !_x})()&&(function(){var _m=_i;return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$".indexOf(_s[_i])>=0)&&(++_i,true)&&((function(){while(SYMBOL_CHAR($));})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function FloatLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&(_s[_i]===".")&&(++_i,true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())&&((function(){var _b=_i;return (function(){var _b=_i;return (_s[_i]==="E")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="e")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&(function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})(),true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function IntegerLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return ((!!_s[_i]&&"-+".indexOf(_s[_i])>=0)&&(++_i,true),true)&&(function(){var _b=_i;return (_s.substr(_i,2)==="0x")&&(_i+=2,true)&&((function(){for(var _n=0;HEX($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="'")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(_s[_i]==="'")&&(++_i,true);_i=_l;return !_x})()&&ASCII($)||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||((function(){for(var _n=0;DIGIT($);++_n);return _n>0})())||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function StringLiteral($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="\"")&&(++_i,true)&&((function(){while((function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\"\\\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="\\")&&(++_i,true)&&(function(){var _b=_i;return (!!_s[_i]&&"\"\\bfnrt".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="u")&&(++_i,true)&&HEX($)&&HEX($)&&HEX($)&&HEX($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})());})(),true)&&(_s[_i]==="\"")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()&&_($)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
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
function INLINE($){return (function(){var _b=_i;return (_s.substr(_i,6)==="inline")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
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
function RETURN($){return (function(){var _b=_i;return (_s.substr(_i,6)==="return")&&(_i+=6,true)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
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
function PREFIX_OP($){return (function(){var _b=_i;return (function(){var _m=_i;return (function(){var _b=_i;return (_s[_i]==="-")&&(++_i,true)&&(function(){var _l=_i,_x=(function(){var _b=_i;return (_s[_i]==="'")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||DIGIT($)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="-")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})();_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="~")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="&")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||(_s[_i]==="*")&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)||BUILT_IN($)&&(function(){var _l=_i,_x=SYMBOL_CHAR($);_i=_l;return !_x})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()&&($._=_s.slice(_m,_i),true)})()||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function SYMBOL_CHAR($){return (function(){var _b=_i;return (!!_s[_i]&&"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function HEX($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789ABCDEFabcdef".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function DIGIT($){return (function(){var _b=_i;return (!!_s[_i]&&"0123456789".indexOf(_s[_i])>=0)&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
function ASCII($){return (function(){var _b=_i;return (function(){var _l=_i,_x=(!!_s[_i]&&"\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c\u001d\u001e\u001f ¡¢£¤¥¦§¨©ª«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ".indexOf(_s[_i])>=0)&&(++_i,true);_i=_l;return !_x})()&&(!!_s[_i])&&(++_i,true)||(_im=(_i>_im?_i:_im),_i=_b,false)})()};
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
