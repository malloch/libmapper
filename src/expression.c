#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mapper_internal.h"

#define MAX_HISTORY -100
#define STACK_SIZE 128
#define VAR_SIZE 8
#ifdef DEBUG
#define TRACING 0 /* Set non-zero to see trace during parse & eval. */
#else
#define TRACING 0
#endif

#define lex_error trace
#define parse_error trace

static int mini(int x, int y)
{
    if (y < x) return y;
    else return x;
}

static float minf(float x, float y)
{
    if (y < x) return y;
    else return x;
}

static double mind(double x, double y)
{
    if (y < x) return y;
    else return x;
}

static int maxi(int x, int y)
{
    if (y > x) return y;
    else return x;
}

static float maxf(float x, float y)
{
    if (y > x) return y;
    else return x;
}

static double maxd(double x, double y)
{
    if (y > x) return y;
    else return x;
}

static float pif()
{
    return M_PI;
}

static double pid()
{
    return M_PI;
}

static float ef()
{
    return M_E;
}

static double ed()
{
    return M_E;
}

static float midiToHzf(float x)
{
    return 440. * pow(2.0, (x - 69) / 12.0);
}

static double midiToHzd(double x)
{
    return 440. * pow(2.0, (x - 69) / 12.0);
}

static float hzToMidif(float x)
{
    return 69. + 12. * log2(x / 440.);
}

static double hzToMidid(double x)
{
    return 69. + 12. * log2(x / 440.);
}

static float uniformf(float x)
{
    return rand() / (RAND_MAX + 1.0) * x;
}

static double uniformd(double x)
{
    return rand() / (RAND_MAX + 1.0) * x;
}

typedef enum {
    VAR_UNKNOWN=-1,
    VAR_X=0,
    VAR_Y,
    N_VARS
} expr_var_t;

const char *var_strings[] =
{
    "x",
    "y",
};

typedef enum {
    FUNC_UNKNOWN=-1,
    FUNC_ABS=0,
    FUNC_ACOS,
    FUNC_ACOSH,
    FUNC_ASIN,
    FUNC_ASINH,
    FUNC_ATAN,
    FUNC_ATAN2,
    FUNC_ATANH,
    FUNC_CBRT,
    FUNC_CEIL,
    FUNC_COS,
    FUNC_COSH,
    FUNC_E,
    FUNC_EXP,
    FUNC_EXP2,
    FUNC_FLOOR,
    FUNC_HYPOT,
    FUNC_HZTOMIDI,
    FUNC_LOG,
    FUNC_LOG10,
    FUNC_LOG2,
    FUNC_LOGB,
    FUNC_MAX,
    FUNC_MIDITOHZ,
    FUNC_MIN,
    FUNC_PI,
    FUNC_POW,
    FUNC_ROUND,
    FUNC_SIN,
    FUNC_SINH,
    FUNC_SQRT,
    FUNC_TAN,
    FUNC_TANH,
    FUNC_TRUNC,
    /* place functions which should never be precomputed below this point */
    FUNC_UNIFORM,
    N_FUNCS
} expr_func_t;

static struct {
    const char *name;
    unsigned int arity;
    void *func_int32;
    void *func_float;
    void *func_double;
} function_table[] = {
    { "abs",      1,    abs,        fabsf,      fabs        },
    { "acos",     1,    0,          acosf,      acos        },
    { "acosh",    1,    0,          acoshf,     acosh       },
    { "asin",     1,    0,          asinf,      asin        },
    { "asinh",    1,    0,          asinhf,     asinh       },
    { "atan",     1,    0,          atanf,      atan        },
    { "atan2",    2,    0,          atan2f,     atan2       },
    { "atanh",    1,    0,          atanhf,     atanh       },
    { "cbrt",     1,    0,          cbrtf,      cbrt        },
    { "ceil",     1,    0,          ceilf,      ceil        },
    { "cos",      1,    0,          cosf,       cos         },
    { "cosh",     1,    0,          coshf,      cosh        },
    { "e",        0,    0,          ef,         ed          },
    { "exp",      1,    0,          expf,       exp         },
    { "exp2",     1,    0,          exp2f,      exp2        },
    { "floor",    1,    0,          floorf,     floor       },
    { "hypot",    2,    0,          hypotf,     hypot       },
    { "hzToMidi", 1,    0,          hzToMidif,  hzToMidid   },
    { "log",      1,    0,          logf,       log         },
    { "log10",    1,    0,          log10f,     log10       },
    { "log2",     1,    0,          log2f,      log2        },
    { "logb",     1,    0,          logbf,      logb        },
    { "max",      2,    maxi,       maxf,       maxd        },
    { "midiToHz", 1,    0,          midiToHzf,  midiToHzd   },
    { "min",      2,    mini,       minf,       mind        },
    { "pi",       0,    0,          pif,        pid         },
    { "pow",      2,    0,          powf,       pow         },
    { "round",    1,    0,          roundf,     round       },
    { "sin",      1,    0,          sinf,       sin         },
    { "sinh",     1,    0,          sinhf,      sinh        },
    { "sqrt",     1,    0,          sqrtf,      sqrt        },
    { "tan",      1,    0,          tanf,       tan         },
    { "tanh",     1,    0,          tanhf,      tanh        },
    { "trunc",    1,    0,          truncf,     trunc       },
    /* place functions which should never be precomputed below this point */
    { "uniform",  1,    0,          uniformf,   uniformd    },
};

typedef enum {
    VFUNC_UNKNOWN=-1,
    VFUNC_ALL=0,
    VFUNC_ANY,
    N_VFUNCS
} expr_vfunc_t;

const char *vfunc_strings[] =
{
    "all",
    "any",
};

typedef enum {
    OP_LOGICAL_NOT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_ADD,
    OP_SUBTRACT,
    OP_LEFT_BIT_SHIFT,
    OP_RIGHT_BIT_SHIFT,
    OP_IS_GREATER_THAN,
    OP_IS_GREATER_THAN_OR_EQUAL,
    OP_IS_LESS_THAN,
    OP_IS_LESS_THAN_OR_EQUAL,
    OP_IS_EQUAL,
    OP_IS_NOT_EQUAL,
    OP_BITWISE_AND,
    OP_BITWISE_XOR,
    OP_BITWISE_OR,
    OP_LOGICAL_AND,
    OP_LOGICAL_OR,
    OP_CONDITIONAL_IF_THEN,
    OP_CONDITIONAL_IF_ELSE,
    OP_CONDITIONAL_IF_THEN_ELSE,
} expr_op_t;

static struct {
    const char *name;
    unsigned int arity;
    unsigned int precedence;
} op_table[] = {
    { "!",          1,  11 },
    { "*",          2,  10 },
    { "/",          2,  10 },
    { "%",          2,  10 },
    { "+",          2,   9 },
    { "-",          2,   9 },
    { "<<",         2,   8 },
    { ">>",         2,   8 },
    { ">",          2,   7 },
    { ">=",         2,   7 },
    { "<",          2,   7 },
    { "<=",         2,   7 },
    { "==",         2,   6 },
    { "!=",         2,   6 },
    { "&",          2,   5 },
    { "^",          2,   4 },
    { "|",          2,   3 },
    { "&&",         2,   2 },
    { "||",         2,   1 },
    { "IFTHEN",     2,   0 },
    { "IFELSE",     2,   0 },
    { "IFTHENELSE", 3,   0 },
};

typedef int func_int32_arity0();
typedef int func_int32_arity1(int);
typedef int func_int32_arity2(int,int);
typedef float func_float_arity0();
typedef float func_float_arity1(float);
typedef float func_float_arity2(float,float);
typedef double func_double_arity0();
typedef double func_double_arity1(double);
typedef double func_double_arity2(double,double);

typedef union _mapper_signal_value {
    float f;
    double d;
    int i32;
} mapper_signal_value_t, mval;

typedef struct _token {
    enum {
        TOK_CONST           = 0x0001,
        TOK_NEGATE          = 0x0002,
        TOK_FUNC            = 0x0003,
        TOK_VFUNC           = 0x0004,
        TOK_OPEN_PAREN      = 0x0005,
        TOK_OPEN_SQUARE     = 0x0010,
        TOK_OPEN_CURLY      = 0x0020,
        TOK_CLOSE_PAREN     = 0x0040,
        TOK_CLOSE_SQUARE    = 0x0080,
        TOK_CLOSE_CURLY     = 0x0100,
        TOK_VAR             = 0x0200,
        TOK_OP              = 0x0400,
        TOK_COMMA           = 0x0800,
        TOK_COLON           = 0x1000,
        TOK_ASSIGNMENT      = 0x2000,
        TOK_TT_ASSIGNMENT   = 0x4000,
        TOK_TIMETAG         = 0x8000,
        TOK_VECTORIZE,
        TOK_END,
    } toktype;
    union {
        float f;
        int i;
        double d;
        expr_var_t var;
        expr_op_t op;
        expr_func_t func;
        expr_vfunc_t vfunc;
    };
    union {
        char casttype;
        int assignment_offset;
    };
    int vector_length;
    union {
        int vector_index;
        int vectorizer_arity;
    };
    char history_index;
    char vector_length_locked;
    char datatype;
} mapper_token_t, *mapper_token;

typedef struct _variable {
    char *name;
    int vector_index;
    int vector_length;
    char datatype;
    char casttype;
    char history_size;          // limited to 100 in parser anyway
    char vector_length_locked;
    char assigned;
} mapper_variable_t, *mapper_variable;

static expr_func_t function_lookup(const char *s, int len)
{
    int i;
    for (i=0; i<N_FUNCS; i++) {
        if (strncmp(s, function_table[i].name, len)==0)
            return i;
    }
    return FUNC_UNKNOWN;
}

static expr_vfunc_t vfunction_lookup(const char *s, int len)
{
    int i;
    for (i=0; i<N_VFUNCS; i++) {
        if (strncmp(s, vfunc_strings[i], len)==0)
            return i;
    }
    return VFUNC_UNKNOWN;
}

static expr_var_t variable_lookup(const char *s, int len)
{
    int i;
    for (i=0; i<N_VARS; i++) {
        if (strncmp(s, var_strings[i], len)==0)
            return i;
    }
    return VAR_UNKNOWN;
}

static int expr_lex(const char *str, int index, mapper_token_t *tok)
{
    tok->datatype = 'i';
    tok->casttype = 0;
    tok->vector_length = 1;
    tok->vector_index = 0;
    tok->vector_length_locked = 0;
    int n, i;
    char c = str[index];
    int integer_found = 0;

    if (c==0) {
        tok->toktype = TOK_END;
        return index;
    }

  again:

    i = index;
    if (isdigit(c)) {
        do {
            c = str[++index];
        } while (c && isdigit(c));
        n = atoi(str+i);
        integer_found = 1;
        if (c!='.' && c!='e') {
            tok->i = n;
            tok->toktype = TOK_CONST;
            tok->datatype = 'i';
            return index;
        }
    }

    switch (c) {
    case '.':
        c = str[++index];
        if (integer_found) {
            if (!isdigit(c) && c!='e') {
                tok->toktype = TOK_CONST;
                tok->f = (float)n;
                tok->datatype = 'f';
                return index;
            }
        }
        else if (c == 't' && (c = str[++index]) && c == 't') {
            // variable timetag syntax
            tok->toktype = TOK_TIMETAG;
            return ++index;
        }
        else if (!isdigit(c))
            break;
        do {
            c = str[++index];
        } while (c && isdigit(c));
        if (c!='e') {
            tok->f = atof(str+i);
            tok->toktype = TOK_CONST;
            tok->datatype = 'f';
            return index;
        }
    case 'e':
        if (!integer_found) {
            n = index;
            while (c && (isalpha(c) || isdigit(c)))
                c = str[++index];
            tok->toktype = TOK_FUNC;
            if ((tok->func = function_lookup(str+i, index-i)) != FUNC_UNKNOWN) {
                tok->toktype = TOK_FUNC;
            }
            else if ((tok->vfunc = vfunction_lookup(str+i, index-i)) != VFUNC_UNKNOWN) {
                tok->toktype = TOK_VFUNC;
            }
            else {
                tok->var = variable_lookup(str+i, index-i);
                tok->toktype = TOK_VAR;
            }
            return index;
        }
        c = str[++index];
        if (c!='-' && c!='+' && !isdigit(c)) {
            lex_error("Incomplete scientific notation `%s'.\n", str+i);
            break;
        }
        if (c=='-' || c=='+')
            c = str[++index];
        while (c && isdigit(c))
            c = str[++index];
        tok->toktype = TOK_CONST;
        tok->datatype = 'f';
        tok->f = atof(str+i);
        return index;
    case '+':
        tok->toktype = TOK_OP;
        tok->op = OP_ADD;
        return ++index;
    case '-':
        // could be either subtraction or negation
        i = index-1;
        // back up one character
        while (i && strchr(" \t\r\n", str[i]))
           i--;
        if (isalpha(str[i]) || isdigit(str[i]) || strchr(")]}", str[i])) {
            tok->toktype = TOK_OP;
            tok->op = OP_SUBTRACT;
        }
        else {
            tok->toktype = TOK_NEGATE;
        }
        return ++index;
    case '/':
        tok->toktype = TOK_OP;
        tok->op = OP_DIVIDE;
        return ++index;
    case '*':
        tok->toktype = TOK_OP;
        tok->op = OP_MULTIPLY;
        return ++index;
    case '%':
        tok->toktype = TOK_OP;
        tok->op = OP_MODULO;
        return ++index;
    case '=':
        // could be '=', '=='
        c = str[++index];
        if (c == '=') {
            tok->toktype = TOK_OP;
            tok->op = OP_IS_EQUAL;
            ++index;
        }
        else {
            tok->toktype = TOK_ASSIGNMENT;
        }
        return index;
    case '<':
        // could be '<', '<=', '<<'
        tok->toktype = TOK_OP;
        tok->op = OP_IS_LESS_THAN;
        c = str[++index];
        if (c == '=') {
            tok->op = OP_IS_LESS_THAN_OR_EQUAL;
            ++index;
        }
        else if (c == '<') {
            tok->op = OP_LEFT_BIT_SHIFT;
            ++index;
        }
        return index;
    case '>':
        // could be '>', '>=', '>>'
        tok->toktype = TOK_OP;
        tok->op = OP_IS_GREATER_THAN;
        c = str[++index];
        if (c == '=') {
            tok->op = OP_IS_GREATER_THAN_OR_EQUAL;
            ++index;
        }
        else if (c == '>') {
            tok->op = OP_RIGHT_BIT_SHIFT;
            ++index;
        }
        return index;
    case '!':
        // could be '!', '!='
        // TODO: handle factorial case
        tok->toktype = TOK_OP;
        tok->op = OP_LOGICAL_NOT;
        c = str[++index];
        if (c == '=') {
            tok->op = OP_IS_NOT_EQUAL;
            ++index;
        }
        return index;
    case '&':
        // could be '&', '&&'
        tok->toktype = TOK_OP;
        tok->op = OP_BITWISE_AND;
        c = str[++index];
        if (c == '&') {
            tok->op = OP_LOGICAL_AND;
            ++index;
        }
        return index;
    case '|':
        // could be '|', '||'
        tok->toktype = TOK_OP;
        tok->op = OP_BITWISE_OR;
        c = str[++index];
        if (c == '|') {
            tok->op = OP_LOGICAL_OR;
            ++index;
        }
        return index;
    case '^':
        // bitwise XOR
        tok->toktype = TOK_OP;
        tok->op = OP_BITWISE_XOR;
        return ++index;
    case '(':
        tok->toktype = TOK_OPEN_PAREN;
        return ++index;
    case ')':
        tok->toktype = TOK_CLOSE_PAREN;
        return ++index;
    case '[':
        tok->toktype = TOK_OPEN_SQUARE;
        return ++index;
    case ']':
        tok->toktype = TOK_CLOSE_SQUARE;
        return ++index;
    case '{':
        tok->toktype = TOK_OPEN_CURLY;
        return ++index;
    case '}':
        tok->toktype = TOK_CLOSE_CURLY;
        return ++index;
    case ' ':
    case '\t':
    case '\r':
    case '\n':
        c = str[++index];
        goto again;
    case ',':
        tok->toktype = TOK_COMMA;
        return ++index;
    case '?':
        // conditional
        tok->toktype = TOK_OP;
        tok->op = OP_CONDITIONAL_IF_THEN;
        c = str[++index];
        if (c == ':') {
            tok->op = OP_CONDITIONAL_IF_ELSE;
            ++index;
        }
        return index;
    case ':':
        tok->toktype = TOK_COLON;
        return ++index;
    default:
        if (!isalpha(c)) {
            lex_error("unknown character '%c' in lexer\n", c);
            break;
        }
        while (c && (isalpha(c) || isdigit(c)))
            c = str[++index];
        if ((tok->func = function_lookup(str+i, index-i)) != FUNC_UNKNOWN) {
            tok->toktype = TOK_FUNC;
        }
        else if ((tok->vfunc = vfunction_lookup(str+i, index-i)) != VFUNC_UNKNOWN) {
            tok->toktype = TOK_VFUNC;
        }
        else {
            tok->var = variable_lookup(str+i, index-i);
            tok->toktype = TOK_VAR;
        }
        return index;
    }

    return 1;
}

struct _mapper_expr
{
    mapper_token tokens;
    mapper_token start;
    int length;
    int vector_size;
    int input_history_size;
    int output_history_size;
    mapper_variable variables;
    int num_variables;
    int constant_output;
};

void mapper_expr_free(mapper_expr expr)
{
    int i;
    if (expr->tokens)
        free(expr->tokens);
    if (expr->num_variables && expr->variables) {
        for (i = 0; i < expr->num_variables; i++) {
            free(expr->variables[i].name);
        }
        free(expr->variables);
    }
    free(expr);
}

#ifdef DEBUG

void printtoken(mapper_token_t tok)
{
    int i;
    char tokstr[32];
    switch (tok.toktype) {
        case TOK_CONST:
            switch (tok.datatype) {
                case 'f':   snprintf(tokstr, 32, "%f", tok.f);  break;
                case 'd':   snprintf(tokstr, 32, "%f", tok.d);  break;
                case 'i':   snprintf(tokstr, 32, "%d", tok.i);  break;
            }                                                   break;
        case TOK_OP:
            snprintf(tokstr, 32, "%s", op_table[tok.op].name);  break;
        case TOK_OPEN_CURLY:    snprintf(tokstr, 32, "{");      break;
        case TOK_OPEN_PAREN:    snprintf(tokstr, 32, "(");      break;
        case TOK_OPEN_SQUARE:   snprintf(tokstr, 32, "[");      break;
        case TOK_CLOSE_CURLY:   snprintf(tokstr, 32, "}");      break;
        case TOK_CLOSE_PAREN:   snprintf(tokstr, 32, ")");      break;
        case TOK_CLOSE_SQUARE:  snprintf(tokstr, 32, "]");      break;
        case TOK_VAR:
            if (tok.var<N_VARS)
                snprintf(tokstr, 32, "%s{%d}[%d]", var_strings[tok.var],
                         tok.history_index, tok.vector_index);
            else
                snprintf(tokstr, 32, "var%d{%d}[%d]", tok.var-N_VARS,
                         tok.history_index, tok.vector_index);
            break;
        case TOK_TIMETAG:
            if (tok.var<N_VARS)
                snprintf(tokstr, 32, "%s{%d}.tt", var_strings[tok.var],
                         tok.history_index);
            else
                snprintf(tokstr, 32, "var%d{%d}.tt", tok.var-N_VARS,
                         tok.history_index);
            break;
        case TOK_FUNC:
            snprintf(tokstr, 32, "%s()", function_table[tok.func].name);
            break;
        case TOK_COMMA:         snprintf(tokstr, 32, ",");      break;
        case TOK_COLON:         snprintf(tokstr, 32, ":");      break;
        case TOK_VECTORIZE:
            snprintf(tokstr, 32, "VECT(%d)", tok.vectorizer_arity);
            break;
        case TOK_NEGATE:        snprintf(tokstr, 32, "-");      break;
        case TOK_VFUNC:
            snprintf(tokstr, 32, "%s()", vfunc_strings[tok.func]);
            break;
        case TOK_ASSIGNMENT:
            if (tok.var<N_VARS)
                snprintf(tokstr, 32, "ASSIGN_TO:%s{%d}[%d]->[%d]",
                         var_strings[tok.var], tok.history_index,
                         tok.assignment_offset, tok.vector_index);
            else
                snprintf(tokstr, 32, "ASSIGN_TO:var%d{%d}[%d]->[%d]",
                         tok.var-N_VARS, tok.history_index,
                         tok.assignment_offset, tok.vector_index);
            break;
        case TOK_TT_ASSIGNMENT:
            if (tok.var<N_VARS)
                snprintf(tokstr, 32, "ASSIGN_TO:%s{%d}.tt",
                         var_strings[tok.var], tok.history_index);
            else
                snprintf(tokstr, 32, "ASSIGN_TO:var%d{%d}.tt",
                         tok.var-N_VARS, tok.history_index);
            break;
        case TOK_END:       printf("END\n");                    return;
        default:            printf("(unknown token)");          return;
    }
    printf("%s", tokstr);
    // indent
    int len = strlen(tokstr);
    for (i = len; i < 30; i++) {
        printf(" ");
    }
    printf("%c[%d]", tok.datatype, tok.vector_length);
    if (tok.vector_length_locked)
        printf("L");
    if (tok.casttype)
        printf("->%c", tok.casttype);
    printf("\n");
}

void printstack(const char *s, mapper_token_t *stack, int top)
{
    int i, j, indent = 0;
    printf("%s ", s);
    if (s)
        indent = strlen(s) + 1;
    for (i=0; i<=top; i++) {
        if (i != 0) {
            for (j=0; j<indent; j++)
                printf(" ");
        }
        printtoken(stack[i]);
    }
    printf("\n");
}

void printexpr(const char *s, mapper_expr e)
{
    printstack(s, e->tokens, e->length-1);
}

#endif

static char compare_token_datatype(mapper_token_t tok, char type)
{
    // return the higher datatype
    if (tok.datatype == 'd' || type == 'd')
        return 'd';
    else if (tok.datatype == 'f' || type == 'f')
        return 'f';
    else
        return 'i';
}

static char promote_token_datatype(mapper_token_t *tok, char type)
{
    tok->casttype = 0;

    if (tok->datatype == type)
        return type;

    if (tok->toktype == TOK_ASSIGNMENT) {
        if (tok->var < N_VARS) {
            // typecasting is not possible
            return tok->datatype;
        }
        else {
            // user-defined variable, can typecast
            tok->casttype = type;
            return type;
        }
    }

    if (tok->toktype == TOK_CONST) {
        // constants can be cast immediately
        if (tok->datatype == 'i') {
            if (type == 'f') {
                tok->f = (float)tok->i;
                tok->datatype = type;
            }
            else if (type == 'd') {
                tok->d = (double)tok->i;
                tok->datatype = type;
            }
        }
        else if (tok->datatype == 'f') {
            if (type == 'd') {
                tok->d = (double)tok->f;
                tok->datatype = type;
            }
            else if (type == 'i') {
                tok->casttype = type;
            }
        }
        else {
            tok->casttype = type;
        }
        return type;
    }
    else if (tok->toktype == TOK_VAR) {
        // we need to cast at runtime
        tok->casttype = type;
        return type;
    }
    else {
        if (tok->datatype == 'i' || type == 'd') {
            tok->datatype = type;
            return type;
        }
        else {
            tok->casttype = type;
            return tok->datatype;
        }
    }
    return type;
}

static void lock_vector_lengths(mapper_token_t *stack, int top)
{
    int i=top, arity=1;

    while ((i >= 0) && arity--) {
        stack[i].vector_length_locked = 1;
        if (stack[i].toktype == TOK_OP)
            arity += op_table[stack[i].op].arity;
        else if (stack[i].toktype == TOK_FUNC)
            arity += function_table[stack[i].func].arity;
        else if (stack[i].toktype == TOK_VECTORIZE)
            arity += stack[i].vectorizer_arity;
        i--;
    }
}

static int check_types_and_lengths(mapper_token_t *stack, int top)
{
    // TODO: allow precomputation of const-only vectors
    int i, arity, can_precompute = 1;
    char type = stack[top].datatype;
    int vector_length = stack[top].vector_length;

    switch (stack[top].toktype) {
        case TOK_OP:
            arity = op_table[stack[top].op].arity;
            break;
        case TOK_FUNC:
            arity = function_table[stack[top].func].arity;
            if (stack[top].func >= FUNC_UNIFORM)
                can_precompute = 0;
            break;
        case TOK_VFUNC:
            arity = 1;
            break;
        case TOK_VECTORIZE:
            arity = stack[top].vectorizer_arity;
            can_precompute = 0;
            break;
        case TOK_ASSIGNMENT:
        case TOK_TT_ASSIGNMENT:
            arity = 1;
            can_precompute = 0;
            break;
        default:
            return top;
    }

    if (arity) {
        // find operator or function inputs
        i = top;
        int skip = 0;
        int depth = arity;
        // last arg of op or func is at top-1
        type = compare_token_datatype(stack[top-1], type);
        if (stack[top-1].vector_length > vector_length)
            vector_length = stack[top-1].vector_length;

        /* Walk down stack distance of arity, checking datatypes
         * and vector lengths. */
        while (--i >= 0) {
            if (stack[i].toktype == TOK_FUNC &&
                function_table[stack[i].func].arity)
                can_precompute = 0;
            else if (stack[i].toktype != TOK_CONST)
                can_precompute = 0;

            if (skip == 0) {
                type = compare_token_datatype(stack[i], type);
                if (stack[i].toktype == TOK_VFUNC)
                    stack[i].vector_length = vector_length;
                else if (stack[i].vector_length > vector_length)
                    vector_length = stack[i].vector_length;
                depth--;
                if (depth == 0)
                    break;
            }
            else
                skip--;
            if (stack[i].toktype == TOK_OP)
                skip += op_table[stack[i].op].arity;
            else if (stack[i].toktype == TOK_FUNC)
                skip += function_table[stack[i].func].arity;
            else if (stack[i].toktype == TOK_VFUNC)
                skip += 1;
            else if (stack[i].toktype == TOK_VECTORIZE)
                skip += stack[i].vectorizer_arity;
        }

        if (depth)
            return -1;

        /* walk down stack distance of arity again, promoting datatypes
         * and vector lengths */
        i = top;
        if (stack[top].toktype == TOK_VECTORIZE) {
            skip = stack[top].vectorizer_arity;
            depth = 0;
        }
        else if (stack[top].toktype == TOK_VFUNC) {
            skip = 1;
            depth = 0;
        }
        else {
            skip = 0;
            depth = arity;
        }
        type = promote_token_datatype(&stack[i], type);
        while (--i >= 0) {
            // we will promote types within range of compound arity
            type = promote_token_datatype(&stack[i], type);

            if (skip <= 0) {
                // also check/promote vector length
                if (!stack[i].vector_length_locked) {
                    if (stack[i].toktype == TOK_VFUNC) {
                        if (stack[i].vector_length != vector_length) {
                            parse_error("Vector length mismatch (%d != %d).\n",
                                        stack[i].vector_length, vector_length);
                            return -1;
                        }
                    }
                    else
                        stack[i].vector_length = vector_length;
                }
                else if (stack[i].vector_length != vector_length) {
                    parse_error("Vector length mismatch (%d != %d).\n",
                                stack[i].vector_length, vector_length);
                    return -1;
                }
            }

            if (stack[i].toktype == TOK_OP) {
                if (skip > 0)
                    skip += op_table[stack[i].op].arity;
                else
                    depth += op_table[stack[i].op].arity;
            }
            else if (stack[i].toktype == TOK_FUNC) {
                if (skip > 0)
                    skip += function_table[stack[i].func].arity;
                else
                    depth += function_table[stack[i].func].arity;
            }
            else if (stack[i].toktype == TOK_VFUNC)
                skip = 2;
            else if (stack[i].toktype == TOK_VECTORIZE)
                skip = stack[i].vectorizer_arity + 1;

            if (skip > 0)
                skip--;
            else
                depth--;
            if (depth <= 0 && skip <= 0)
                break;
        }

        if (!stack[top].vector_length_locked) {
            if (stack[top].toktype != TOK_VFUNC)
                stack[top].vector_length = vector_length;
        }
        else if (stack[top].vector_length != vector_length) {
            parse_error("Vector length mismatch (%d != %d).\n",
                        stack[top].vector_length, vector_length);
            return -1;
        }
    }
    else {
        stack[top].datatype = 'f';
    }

    // if stack within bounds of arity was only constants, we're ok to compute
    if (!can_precompute)
        return top;

    struct _mapper_expr e;
    e.start = &stack[top-arity];
    e.length = arity+1;
    e.vector_size = vector_length;
    e.variables = 0;
    e.num_variables = 0;
    mapper_signal_history_t h;
    // TODO: this variable should not be mapper_signal_value_t?
    mapper_signal_value_t v;
    mapper_timetag_t tt;
    h.type = stack[top].datatype;
    h.value = &v;
    h.timetag = &tt;
    h.position = -1;
    h.length = 1;
    h.size = 1;
    
    if (!mapper_expr_evaluate(&e, 0, 0, &h, 0))
        return top;

    switch (stack[top].datatype) {
        case 'f':
            stack[top-arity].f = v.f;
            break;
        case 'd':
            stack[top-arity].d = v.d;
            break;
        case 'i':
            stack[top-arity].i = v.i32;
            break;
        default:
            return 0;
            break;
    }
    stack[top-arity].toktype = TOK_CONST;
    return top-arity;
}

static int check_assignment_types_and_lengths(mapper_token_t *stack, int top)
{
    int i = top, vector_length = 0;
    expr_var_t var = stack[top].var;

    while (i >= 0 && (stack[i].toktype == TOK_ASSIGNMENT
                      || stack[i].toktype == TOK_TT_ASSIGNMENT)) {
        if (stack[i].var != var) {
            parse_error("Cannot mix variable references in assignment\n");
            return -1;
        }
        vector_length += stack[i].vector_length;
        i--;
    }
    if (i < 0) {
        parse_error("Malformed expression.");
        return -1;
    }
    if (stack[i].vector_length != vector_length) {
        parse_error("Vector length mismatch (%d != %d).\n",
                    stack[i].vector_length, vector_length);
        return -1;
    }
    promote_token_datatype(&stack[i], stack[top].datatype);
    if (check_types_and_lengths(stack, i) == -1)
        return -1;
    promote_token_datatype(&stack[i], stack[top].datatype);

    if (stack[top].history_index == 0 || i == 0)
        return 0;

    // Move assignment expression to beginning of stack
    int j = 0, expr_length = top-i+1;
    if (stack[i].toktype == TOK_VECTORIZE)
        expr_length += stack[i].vectorizer_arity;

    mapper_token_t temp[expr_length];
    for (i = top-expr_length+1; i <= top; i++)
        memcpy(&temp[j++], &stack[i], sizeof(mapper_token_t));

    for (i = top-expr_length; i >= 0; i--)
        memcpy(&stack[i+expr_length], &stack[i], sizeof(mapper_token_t));

    for (i = 0; i < expr_length; i++)
        memcpy(&stack[i], &temp[i], sizeof(mapper_token_t));

    return 0;
}

/* Macros to help express stack operations in parser. */
#define FAIL(msg) {                                                 \
    parse_error("%s\n", msg);                                       \
    while (--num_variables >= 0)                                    \
        free(variables[num_variables].name);                        \
    return 0;                                                       \
}
#define PUSH_TO_OUTPUT(x)                                           \
{                                                                   \
    if (++outstack_index >= STACK_SIZE)                             \
        {FAIL("Stack size exceeded.");}                             \
    memcpy(outstack + outstack_index, &x, sizeof(mapper_token_t));  \
}
#define PUSH_TO_OPERATOR(x)                                         \
{                                                                   \
    if (++opstack_index >= STACK_SIZE)                              \
        {FAIL("Stack size exceeded.");}                             \
    memcpy(opstack + opstack_index, &x, sizeof(mapper_token_t));    \
}
#define POP_OPERATOR() ( opstack_index-- )
#define POP_OPERATOR_TO_OUTPUT()                                    \
{                                                                   \
    PUSH_TO_OUTPUT(opstack[opstack_index]);                         \
    outstack_index = check_types_and_lengths(outstack,              \
                                             outstack_index);       \
    if (outstack_index < 0)                                         \
         {FAIL("Malformed expression.");}                           \
    POP_OPERATOR();                                                 \
}
#define GET_NEXT_TOKEN(x)                                           \
{                                                                   \
    lex_index = expr_lex(str, lex_index, &x);                       \
    if (!lex_index)                                                 \
        {FAIL("Error in lexer.");}                                  \
}

/*! Use Dijkstra's shunting-yard algorithm to parse expression into RPN stack. */
mapper_expr mapper_expr_new_from_string(const char *str,
                                        char input_type,
                                        char output_type,
                                        int input_vector_size,
                                        int output_vector_size)
{
    if (!str) return 0;
    if (input_type != 'i' && input_type != 'f' && input_type != 'd') return 0;
    if (output_type != 'i' && output_type != 'f' && output_type != 'd') return 0;

    mapper_token_t outstack[STACK_SIZE];
    mapper_token_t opstack[STACK_SIZE];
    int i, lex_index = 0, outstack_index = -1, opstack_index = -1;
    int oldest_input = 0, oldest_output = 0, max_vector = 1;

    int assigning = 0;
    int output_assigned = 0;
    int vectorizing = 0;
    int variable = 0;
    int allow_toktype = 0xFFFF;
    int constant_output = 1;

    mapper_variable_t variables[VAR_SIZE];
    int num_variables = 0;

    int assign_mask = TOK_VAR | TOK_OPEN_SQUARE | TOK_COMMA | TOK_CLOSE_SQUARE;
    int OBJECT_TOKENS = TOK_VAR | TOK_CONST | TOK_FUNC | TOK_VFUNC |
                        TOK_NEGATE | TOK_OPEN_PAREN | TOK_OPEN_SQUARE;

    mapper_token_t tok;

    // all expressions must start with assignment e.g. "y=" (ignoring spaces)
    while (str[lex_index] == ' ') lex_index++;
    if (!str[lex_index])
        {FAIL("No expression found.");}

    assigning = 1;
    allow_toktype = TOK_VAR | TOK_OPEN_SQUARE;

    while (str[lex_index]) {
        GET_NEXT_TOKEN(tok);
        if (variable && tok.toktype != TOK_OPEN_SQUARE
            && tok.toktype != TOK_OPEN_CURLY && tok.toktype != TOK_TIMETAG)
        {
            // check that 'y' has not been used with implicit {0} history index
            if (!assigning && outstack[outstack_index].var == VAR_Y &&
                outstack[outstack_index].history_index > -1)
                {FAIL("Output history index cannot be > -1.");}
            variable = 0;
        }
        if (!((tok.toktype & allow_toktype & (assigning ? assign_mask : 0xFFFF))
              | (assigning ? TOK_ASSIGNMENT : 0))) {
            {FAIL("Illegal token sequence.");}
        }
        switch (tok.toktype) {
            case TOK_CONST:
                // push to output stack
                PUSH_TO_OUTPUT(tok);
                allow_toktype = TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE |
                                TOK_COMMA | TOK_COLON;
                break;
            case TOK_VAR:
                // set datatype
                if (tok.var == VAR_X) {
                    tok.datatype = input_type;
                    tok.vector_length = input_vector_size;
                    tok.vector_length_locked = 1;
                    constant_output = 0;
                }
                else if (tok.var == VAR_Y) {
                    tok.datatype = output_type;
                    tok.vector_length = output_vector_size;
                    tok.vector_length_locked = 1;
                }
                else {
                    // get name of variable
                    int index = lex_index-1;
                    char c = str[index];
                    while (index >= 0 && c && (isalpha(c) || isdigit(c))) {
                        if (--index >= 0)
                            c = str[index];
                    }

                    // check if variable name matches known variable
                    int i;
                    for (i = 0; i < num_variables; i++) {
                        if (strncmp(variables[i].name, str+index+1, lex_index-index-1)==0) {
                            tok.var = i + N_VARS;
                            tok.datatype = variables[i].datatype;
                            tok.vector_length = variables[i].vector_length;
                            break;
                        }
                    }

                    if (i == num_variables) {
                        if (num_variables >= VAR_SIZE)
                            {FAIL("Maximum number of variables exceeded.");}
                        // need to store new variable
                        variables[num_variables].name = malloc(lex_index-index);
                        snprintf(variables[num_variables].name, lex_index-index,
                                 "%s", str+index+1);
                        variables[num_variables].datatype = 'd';
                        variables[num_variables].vector_length = 1;
                        variables[num_variables].history_size = 1;
                        variables[num_variables].assigned = 0;
#if TRACING
                        printf("Stored new variable '%s' at index %i\n",
                               variables[num_variables].name, num_variables);
#endif
                        tok.var = num_variables + N_VARS;
                        tok.datatype = 'd';
                        num_variables++;
                    }
                }
                tok.history_index = 0;
                tok.vector_index = 0;
                PUSH_TO_OUTPUT(tok);
                // variables can have vector and history indices
                variable = TOK_OPEN_SQUARE | TOK_OPEN_CURLY | TOK_TIMETAG;
                allow_toktype = TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE |
                                TOK_COMMA | TOK_COLON | TOK_TIMETAG |
                                variable | (assigning ? TOK_ASSIGNMENT : 0);
                break;
            case TOK_FUNC:
                if (function_table[tok.func].func_int32)
                    tok.datatype = 'i';
                else
                    tok.datatype = 'f';
                PUSH_TO_OPERATOR(tok);
                if (!function_table[tok.func].arity) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                if (function_table[tok.func].arity)
                    allow_toktype = TOK_OPEN_PAREN;
                else
                    allow_toktype = TOK_OP | TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE |
                                    TOK_COMMA | TOK_COLON;
                if (tok.func >= FUNC_UNIFORM)
                    constant_output = 0;
                break;
            case TOK_VFUNC:
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_OPEN_PAREN;
                break;
            case TOK_OPEN_PAREN:
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_CLOSE_PAREN:
                // pop from operator stack to output until left parenthesis found
                while (opstack_index >= 0 && opstack[opstack_index].toktype != TOK_OPEN_PAREN) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                if (opstack_index < 0)
                    {FAIL("Unmatched parentheses or misplaced comma.");}
                // remove left parenthesis from operator stack
                if (tok.toktype == TOK_CLOSE_PAREN) {
                    POP_OPERATOR();
                    if (opstack[opstack_index].toktype == TOK_FUNC) {
                        // if stack[top] is tok_func, pop to output
                        POP_OPERATOR_TO_OUTPUT();
                    }
                }
                allow_toktype = TOK_OP | TOK_COLON | TOK_COMMA |
                                TOK_CLOSE_PAREN | TOK_CLOSE_SQUARE;
                break;
            case TOK_COMMA:
                // pop from operator stack to output until left parenthesis or TOK_VECTORIZE found
                while (opstack_index >= 0
                       && opstack[opstack_index].toktype != TOK_OPEN_PAREN
                       && opstack[opstack_index].toktype != TOK_VECTORIZE) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                if (opstack_index < 0) {
                    // could be starting another sub-expression
                    if (outstack[outstack_index].toktype != TOK_ASSIGNMENT &&
                        outstack[outstack_index].toktype != TOK_TT_ASSIGNMENT)
                        {FAIL("Malformed expression.");}
                    if (check_assignment_types_and_lengths(outstack, outstack_index) == -1)
                        {FAIL("Malformed expression.");}
                    assigning = 1;
                    allow_toktype = TOK_VAR;
                    break;
                }
                if (opstack[opstack_index].toktype == TOK_VECTORIZE) {
                    opstack[opstack_index].vector_index++;
                    opstack[opstack_index].vector_length +=
                        outstack[outstack_index].vector_length;
                    lock_vector_lengths(outstack, outstack_index);
                }
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_COLON:
                // pop from operator stack to output until conditional found
                while (opstack_index >= 0 &&
                       (opstack[opstack_index].toktype != TOK_OP ||
                        opstack[opstack_index].op != OP_CONDITIONAL_IF_THEN)) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                if (opstack_index < 0)
                    {FAIL("Unmatched colon.");}
                opstack[opstack_index].op = OP_CONDITIONAL_IF_THEN_ELSE;
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_OP:
                // check precedence of operators on stack
                while (opstack_index >= 0 && opstack[opstack_index].toktype == TOK_OP
                       && op_table[opstack[opstack_index].op].precedence >=
                       op_table[tok.op].precedence) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                PUSH_TO_OPERATOR(tok);
                allow_toktype = OBJECT_TOKENS;
                break;
            case TOK_OPEN_SQUARE:
                if (variable & TOK_OPEN_SQUARE) { // vector index not set
                    GET_NEXT_TOKEN(tok);
                    if (tok.toktype != TOK_CONST || tok.datatype != 'i')
                        {FAIL("Non-integer vector index.");}
                    if (outstack[outstack_index].var < VAR_Y) {
                        if (tok.i >= input_vector_size)
                            {FAIL("Index exceeds input vector length.");}
                    }
                    else if (tok.i >= output_vector_size)
                        {FAIL("Index exceeds output vector length.");}
                    outstack[outstack_index].vector_index = tok.i;
                    outstack[outstack_index].vector_length = 1;
                    outstack[outstack_index].vector_length_locked = 1;
                    GET_NEXT_TOKEN(tok);
                    if (tok.toktype == TOK_COLON) {
                        // index is range A:B
                        GET_NEXT_TOKEN(tok);
                        if (tok.toktype != TOK_CONST || tok.datatype != 'i')
                            {FAIL("Malformed vector index.");}
                        if (outstack[outstack_index].var < VAR_Y) {
                            if (tok.i >= input_vector_size)
                                {FAIL("Index exceeds vector length.");}
                        }
                        else if (tok.i >= output_vector_size)
                            {FAIL("Index exceeds vector length.");}
                        if (tok.i <= outstack[outstack_index].vector_index)
                            {FAIL("Malformed vector index.");}
                        outstack[outstack_index].vector_length =
                            tok.i - outstack[outstack_index].vector_index + 1;
                        GET_NEXT_TOKEN(tok);
                    }
                    if (tok.toktype != TOK_CLOSE_SQUARE)
                        {FAIL("Unmatched bracket.");}
                    // vector index set
                    variable &= ~TOK_OPEN_SQUARE;
                    allow_toktype = TOK_OP | TOK_COMMA | TOK_CLOSE_PAREN |
                                    TOK_CLOSE_SQUARE | TOK_COLON |
                                    variable | (assigning ? TOK_ASSIGNMENT : 0);
                }
                else {
                    if (vectorizing)
                        {FAIL("Nested (multidimensional) vectors not allowed.");}
                    tok.toktype = TOK_VECTORIZE;
                    tok.vector_length = 0;
                    tok.vectorizer_arity = 0;
                    PUSH_TO_OPERATOR(tok);
                    vectorizing = 1;
                    allow_toktype = OBJECT_TOKENS & ~TOK_OPEN_SQUARE;
                }
                break;
            case TOK_CLOSE_SQUARE:
                // pop from operator stack to output until TOK_VECTORIZE found
                while (opstack_index >= 0 &&
                       opstack[opstack_index].toktype != TOK_VECTORIZE) {
                    POP_OPERATOR_TO_OUTPUT();
                }
                if (opstack_index < 0)
                    {FAIL("Unmatched brackets or misplaced comma.");}
                if (opstack[opstack_index].vector_length) {
                    opstack[opstack_index].vector_length_locked = 1;
                    opstack[opstack_index].vectorizer_arity++;
                    opstack[opstack_index].vector_length +=
                        outstack[outstack_index].vector_length;
                    lock_vector_lengths(outstack, outstack_index);
                    POP_OPERATOR_TO_OUTPUT();
                }
                else {
                    // we do not need vectorizer token if vector length == 1
                    POP_OPERATOR();
                }
                vectorizing = 0;
                allow_toktype = TOK_OP | TOK_CLOSE_PAREN | TOK_COMMA | TOK_COLON;
                break;
            case TOK_OPEN_CURLY:
                if (!(variable & TOK_OPEN_CURLY)) // history index already set
                    {FAIL("Misplaced brace.");}
                GET_NEXT_TOKEN(tok);
                if (tok.toktype == TOK_NEGATE) {
                    // if negative sign found, get next token
                    outstack[outstack_index].history_index = -1;
                    GET_NEXT_TOKEN(tok);
                }
                if (tok.toktype != TOK_CONST || tok.datatype != 'i')
                    {FAIL("Non-integer history index.");}
                outstack[outstack_index].history_index *= tok.i;
                if (outstack[outstack_index].var == VAR_Y) {
                    if (outstack[outstack_index].history_index > -1)
                        {FAIL("Output history index cannot be > -1.");}
                    else if (outstack[outstack_index].history_index < MAX_HISTORY)
                        {FAIL("Output history index cannot be < -100.");}
                }
                else {
                    if (outstack[outstack_index].history_index > 0)
                    {FAIL("Input history index cannot be > 0.");}
                    else if (outstack[outstack_index].history_index < MAX_HISTORY)
                    {FAIL("Input history index cannot be < -100.");}
                }

                if (outstack[outstack_index].var == VAR_X
                    && outstack[outstack_index].history_index < oldest_input)
                    oldest_input = outstack[outstack_index].history_index;
                else if (outstack[outstack_index].var == VAR_Y
                         && outstack[outstack_index].history_index < oldest_output)
                    oldest_output = outstack[outstack_index].history_index;
                else if (outstack[outstack_index].var >= N_VARS) {
                    // user-defined variable
                    int var_idx = outstack[outstack_index].var - N_VARS;
                    int hist_idx = outstack[outstack_index].history_index;
                    if ((hist_idx * -1 + 1) > variables[var_idx].history_size)
                        variables[var_idx].history_size = hist_idx * -1 + 1;
                }
                GET_NEXT_TOKEN(tok);
                if (tok.toktype != TOK_CLOSE_CURLY)
                    {FAIL("Unmatched brace.");}
                variable &= ~TOK_OPEN_CURLY;
                allow_toktype = TOK_OP | TOK_COMMA | TOK_CLOSE_PAREN |
                                TOK_CLOSE_SQUARE | TOK_COLON |
                                variable | (assigning ? TOK_ASSIGNMENT : 0);
                break;
            case TOK_NEGATE:
                // push '0' to output stack, and '-' to operator stack
                tok.toktype = TOK_CONST;
                tok.datatype = 'i';
                tok.i = 0;
                PUSH_TO_OUTPUT(tok);
                tok.toktype = TOK_OP;
                tok.op = OP_SUBTRACT;
                PUSH_TO_OPERATOR(tok);
                allow_toktype = TOK_CONST | TOK_VAR | TOK_FUNC | TOK_VFUNC;
                break;
            case TOK_ASSIGNMENT:
                // assignment to variable
                if (!assigning)
                    {FAIL("Misplaced assignment operator.");}
                if (opstack_index >= 0 || outstack_index < 0)
                    {FAIL("Malformed expression left of assignment.");}

                if (outstack[outstack_index].toktype == TOK_VAR) {
                    int var = outstack[outstack_index].var;
                    if (var == VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (var == VAR_Y) {
                        if (outstack[outstack_index].history_index == 0) {
                            if (output_assigned)
                                {FAIL("Variable 'y' already assigned.");}
                            output_assigned = 1;
                        }
                    }
                    else if (var >= N_VARS) {
                        if (outstack[outstack_index].history_index == 0)
                            variables[var-N_VARS].assigned = 1;
                    }
                    // nothing extraordinary, continue as normal
                    outstack[outstack_index].toktype = TOK_ASSIGNMENT;
                    outstack[outstack_index].assignment_offset = 0;
                    PUSH_TO_OPERATOR(outstack[outstack_index]);
                    outstack_index--;
                }
                else if (outstack[outstack_index].toktype == TOK_TIMETAG) {
                    // assignment to variable timetag
                    // For now we will only allow assigning to output tt
                    if (outstack[outstack_index].var != VAR_Y)
                        {FAIL("Only output timetag can be written.");}
                    outstack[outstack_index].toktype = TOK_TT_ASSIGNMENT;
                    outstack[outstack_index].datatype = 'd';
                    PUSH_TO_OPERATOR(outstack[outstack_index]);
                    outstack_index--;
                }
                else if (outstack[outstack_index].toktype == TOK_VECTORIZE) {
                    // outstack token is vectorizer
                    outstack_index--;
                    if (outstack[outstack_index].toktype != TOK_VAR)
                        {FAIL("Illegal tokens left of assignment.");}
                    int var = outstack[outstack_index].var;
                    if (var == VAR_X)
                        {FAIL("Cannot assign to input variable 'x'.");}
                    else if (var == VAR_Y) {
                        if (outstack[outstack_index].history_index == 0) {
                            if (output_assigned)
                                {FAIL("Variable 'y' already assigned.");}
                            output_assigned = 1;
                        }
                    }
                    else if (var >= N_VARS) {
                        if (outstack[outstack_index].history_index == 0) {
                            if (variables[var-N_VARS].assigned)
                                {FAIL("Variable already assigned.");}
                            variables[var-N_VARS].assigned = 1;
                        }
                    }
                    while (outstack_index >= 0) {
                        if (outstack[outstack_index].toktype != TOK_VAR)
                            {FAIL("Illegal tokens left of assignment.");}
                        else if (outstack[outstack_index].var != var)
                            {FAIL("Cannot mix variables in vector assignment.");}
                        outstack[outstack_index].toktype = TOK_ASSIGNMENT;
                        PUSH_TO_OPERATOR(outstack[outstack_index]);
                        outstack_index--;
                    }
                    int index = opstack_index, vector_count = 0;
                    while (index >= 0) {
                        opstack[index].assignment_offset = vector_count;
                        vector_count += opstack[index].vector_length;
                        index--;
                    }
                }
                else
                    {FAIL("Malformed expression left of assignment.");}
                assigning = 0;
                allow_toktype = 0xFFFF;
                break;
            case TOK_TIMETAG:
                // only applicable if we are dealing with a variable
                if (!(variable & TOK_TIMETAG)) // history index already set
                    {FAIL("Misplaced timetag property.");}
                // convert preceding TOK_VAR to TOK_TIMETAG
                outstack[outstack_index].toktype = TOK_TIMETAG;
                outstack[outstack_index].datatype = 'd';
                variable &= ~TOK_TIMETAG;
                allow_toktype = TOK_OP | TOK_CLOSE_PAREN | TOK_COMMA | TOK_COLON;
                break;
            default:
                {FAIL("Unknown token type.");}
                break;
        }
#if TRACING
        printstack("OUTPUT STACK:  ", outstack, outstack_index);
        printstack("OPERATOR STACK:", opstack, opstack_index);
#endif
    }

    if (allow_toktype & TOK_CONST || assigning || !output_assigned)
        {FAIL("Expression has no output assignment.");}

    // check that all used-defined variables were assigned
    for (i = 0; i < num_variables; i++) {
        if (!variables[i].assigned)
            {FAIL("User-defined variable not assigned.");}
    }

    // finish popping operators to output, check for unbalanced parentheses
    while (opstack_index >= 0 && opstack[opstack_index].toktype != TOK_ASSIGNMENT) {
        if (opstack[opstack_index].toktype == TOK_OPEN_PAREN)
            {FAIL("Unmatched parentheses or misplaced comma.");}
        POP_OPERATOR_TO_OUTPUT();
    }
    // pop assignment operator(s) to output
    while (opstack_index >= 0) {
        if (opstack[opstack_index].toktype != TOK_ASSIGNMENT
            && opstack[opstack_index].toktype != TOK_TT_ASSIGNMENT)
            {FAIL("Malformed expression.");}
        PUSH_TO_OUTPUT(opstack[opstack_index]);
        POP_OPERATOR();
    }
    // check vector length and type
    if (check_assignment_types_and_lengths(outstack, outstack_index) == -1)
        {FAIL("Malformed expression.");}

#if TRACING
    printstack("--->OUTPUT STACK:", outstack, outstack_index);
    printstack("--->OPERATOR STACK:", opstack, opstack_index);
#endif

    // Check for maximum vector length used in stack
    for (i = 0; i < outstack_index; i++) {
        if (outstack[i].vector_length > max_vector)
            max_vector = outstack[i].vector_length;
    }

    mapper_expr expr = malloc(sizeof(struct _mapper_expr));
    expr->length = outstack_index + 1;

    // copy tokens
    expr->tokens = malloc(sizeof(struct _token)*expr->length);
    memcpy(expr->tokens, &outstack, sizeof(struct _token)*expr->length);
    expr->start = expr->tokens;
    expr->vector_size = max_vector;
    expr->input_history_size = -oldest_input+1;
    expr->output_history_size = -oldest_output+1;
    expr->constant_output = constant_output;

    if (num_variables) {
        // copy user-defined variables
        expr->variables = malloc(sizeof(mapper_variable_t)*num_variables);
        memcpy(expr->variables, variables, sizeof(mapper_variable_t)*num_variables);
    }
    expr->num_variables = num_variables;

    return expr;
}

int mapper_expr_input_history_size(mapper_expr expr)
{
    return expr->input_history_size;
}

int mapper_expr_output_history_size(mapper_expr expr)
{
    return expr->output_history_size;
}

int mapper_expr_num_variables(mapper_expr expr)
{
    return expr->num_variables;
}

int mapper_expr_variable_history_size(mapper_expr expr, int index)
{
    if (index >= expr->num_variables)
        return 0;
    else
        return expr->variables[index].history_size;
}

int mapper_expr_variable_vector_length(mapper_expr expr, int index)
{
    if (index >= expr->num_variables)
        return 0;
    else
        return expr->variables[index].vector_length;
}

int mapper_expr_constant_output(mapper_expr expr)
{
    if (expr->constant_output)
        return 1;
    return 0;
}

#if TRACING
static void print_stack_vector(mapper_signal_value_t *stack, char type,
                               int vector_length)
{
    int i;
    if (vector_length > 1)
        printf("[");
    switch (type) {
        case 'i':
            for (i = 0; i < vector_length; i++)
                printf("%d, ", stack[i].i32);
            break;
        case 'f':
            for (i = 0; i < vector_length; i++)
                printf("%f, ", stack[i].f);
            break;
        case 'd':
            for (i = 0; i < vector_length; i++)
                printf("%f, ", stack[i].d);
            break;
        default:
            break;
    }
    if (vector_length > 1)
        printf("\b\b] ");
    else
        printf("\b\b ");
}
#endif

int mapper_expr_evaluate(mapper_expr expr,
                         mapper_signal_history_t *from,
                         mapper_signal_history_t **expr_vars,
                         mapper_signal_history_t *to,
                         char *typestring)
{
    mapper_signal_value_t stack[expr->length][expr->vector_size];
    int dims[expr->length];

    int i, j, k, top = -1, count = 0, found, updated = 0;
    mapper_token_t *tok = expr->start;

    // init typestring
    if (typestring)
        memset(typestring, 'N', to->length);

    /* Increment index position of output data structure. */
    to->position = (to->position + 1) % to->size;

    while (count < expr->length && tok->toktype != TOK_END) {
        switch (tok->toktype) {
        case TOK_CONST:
            ++top;
            dims[top] = tok->vector_length;
            if (tok->datatype == 'f') {
                for (i = 0; i < tok->vector_length; i++)
                    stack[top][i].f = tok->f;
            }
            else if (tok->datatype == 'd') {
                for (i = 0; i < tok->vector_length; i++)
                    stack[top][i].d = tok->d;
            }
            else if (tok->datatype == 'i') {
                for (i = 0; i < tok->vector_length; i++)
                    stack[top][i].i32 = tok->i;
            }
#if TRACING
            printf("loading constant ");
            print_stack_vector(stack[top], tok->datatype, tok->vector_length);
            printf("\n");
#endif
            break;
        case TOK_VAR:
            {
                int idx;
                switch (tok->var) {
                case VAR_X:
                    ++top;
                    dims[top] = tok->vector_length;
                    idx = ((tok->history_index + from->position
                            + from->size) % from->size);
                    if (from->type == 'd') {
                        double *v = from->value + idx * from->length * mapper_type_size(from->type);
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = v[i+tok->vector_index];
                    }
                    else if (from->type == 'f') {
                        float *v = from->value + idx * from->length * mapper_type_size(from->type);
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = v[i+tok->vector_index];
                    }
                    else if (from->type == 'i') {
                        int *v = from->value + idx * from->length * mapper_type_size(from->type);
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = v[i+tok->vector_index];
                    }
#if TRACING
                    printf("loading variable x{%d}[%d", tok->history_index,
                           tok->vector_index);
                    if (tok->vector_length > 1)
                        printf(":%d", tok->vector_length + tok->vector_index - 1);
                    printf("]: ");
                    print_stack_vector(stack[top], from->type, tok->vector_length);
                    printf("\n");
#endif
                    break;
                case VAR_Y:
                    ++top;
                    dims[top] = tok->vector_length;
                    idx = ((tok->history_index + to->position
                            + to->size) % to->size);
                    if (to->type == 'd') {
                        double *v = to->value + idx * to->length * mapper_type_size(to->type);
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = v[i+tok->vector_index];
                    }
                    else if (to->type == 'f') {
                        float *v = to->value + idx * to->length * mapper_type_size(to->type);
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = v[i+tok->vector_index];
                    }
                    else if (to->type == 'i') {
                        int *v = to->value + idx * to->length * mapper_type_size(to->type);
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = v[i+tok->vector_index];
                    }
#if TRACING
                    printf("loading variable y{%d}[%d", tok->history_index,
                           tok->vector_index);
                    if (tok->vector_length > 1)
                        printf(":%d", tok->vector_length + tok->vector_index - 1);
                    printf("]: ");
                    print_stack_vector(stack[top], to->type, tok->vector_length);
                    printf("\n");
#endif
                    break;
                default:
                    // TODO: allow other data types?
                    if (tok->var > expr->num_variables + N_VARS)
                        goto error;
                    ++top;
                    dims[top] = tok->vector_length;
                    mapper_variable var = &expr->variables[tok->var - N_VARS];
                    mapper_signal_history_t *h = *expr_vars + (tok->var - N_VARS);
                    idx = ((tok->history_index + h->position
                            + var->history_size) % var->history_size);
                    double *v = h->value + idx * var->vector_length * mapper_type_size(var->datatype);
                    for (i = 0; i < tok->vector_length; i++)
                        stack[top][i].d = v[i+tok->vector_index];
#if TRACING
                    printf("loading variable %s{%d}[%d", var->name,
                           tok->history_index, tok->vector_index);
                    if (tok->vector_length > 1)
                        printf(":%d", tok->vector_length + tok->vector_index - 1);
                    printf("]: ");
                    print_stack_vector(stack[top], 'd', tok->vector_length);
                    printf("\n");
#endif
                }
            }
            break;
        case TOK_TIMETAG:
            {
                ++top;
                dims[top] = tok->vector_length;
                mapper_signal_history_t *h;
                int idx;
                if (tok->var == VAR_X) {
                    h = from;
                    idx = ((tok->history_index + from->position
                            + from->size) % from->size);
                }
                else if (tok->var == VAR_Y) {
                    h = to;
                    idx = ((tok->history_index + to->position
                            + to->size) % to->size);
                }
                else if (tok->var > expr->num_variables + N_VARS)
                    goto error;
                else {
                    h = *expr_vars + (tok->var - N_VARS);
                    mapper_variable var = &expr->variables[tok->var - N_VARS];
                    idx = ((tok->history_index + h->position
                            + var->history_size) % var->history_size);
                }
                mapper_timetag_t tt = h->timetag[idx];
                double ttd = mapper_timetag_get_double(tt);
                for (i = 0; i < tok->vector_length; i++)
                    stack[top][i].d = ttd;
#if TRACING
                printf("loading NTP timetag as double %f\n", ttd);
#endif
            }
            break;
        case TOK_OP:
            top -= op_table[tok->op].arity-1;
            dims[top] = tok->vector_length;
#if TRACING
            if (tok->op == OP_CONDITIONAL_IF_THEN || tok->op == OP_CONDITIONAL_IF_ELSE ||
                tok->op == OP_CONDITIONAL_IF_THEN_ELSE) {
                printf("IF ");
                print_stack_vector(stack[top], tok->datatype, tok->vector_length);
                printf("THEN ");
                if (tok->op == OP_CONDITIONAL_IF_ELSE) {
                    print_stack_vector(stack[top], tok->datatype, tok->vector_length);
                    printf("ELSE ");
                    print_stack_vector(stack[top+1], tok->datatype, tok->vector_length);
                }
                else {
                    print_stack_vector(stack[top+1], tok->datatype, tok->vector_length);
                    if (tok->op == OP_CONDITIONAL_IF_THEN_ELSE) {
                        printf("ELSE ");
                        print_stack_vector(stack[top+2], tok->datatype, tok->vector_length);
                    }
                }
            }
            else {
                print_stack_vector(stack[top], tok->datatype, tok->vector_length);
                printf("%s%c ", op_table[tok->op].name, tok->datatype);
                print_stack_vector(stack[top+1], tok->datatype, tok->vector_length);
            }
#endif
            if (tok->datatype == 'f') {
                switch (tok->op) {
                    case OP_ADD:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f + stack[top+1][i].f;
                        break;
                    case OP_SUBTRACT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f - stack[top+1][i].f;
                        break;
                    case OP_MULTIPLY:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f * stack[top+1][i].f;
                        break;
                    case OP_DIVIDE:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f / stack[top+1][i].f;
                        break;
                    case OP_MODULO:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = fmod(stack[top][i].f, stack[top+1][i].f);
                        break;
                    case OP_IS_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f == stack[top+1][i].f;
                        break;
                    case OP_IS_NOT_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f != stack[top+1][i].f;
                        break;
                    case OP_IS_LESS_THAN:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f < stack[top+1][i].f;
                        break;
                    case OP_IS_LESS_THAN_OR_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f <= stack[top+1][i].f;
                        break;
                    case OP_IS_GREATER_THAN:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f > stack[top+1][i].f;
                        break;
                    case OP_IS_GREATER_THAN_OR_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f >= stack[top+1][i].f;
                        break;
                    case OP_LOGICAL_AND:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f && stack[top+1][i].f;
                        break;
                    case OP_LOGICAL_OR:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = stack[top][i].f || stack[top+1][i].f;
                        break;
                    case OP_LOGICAL_NOT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = !stack[top][i].f;
                        break;
                    case OP_CONDITIONAL_IF_THEN:
                        // TODO: should not permit implicit any()/all()
                        for (i = 0; i < tok->vector_length; i++) {
                            if (stack[top][i].f)
                                stack[top][i].f = stack[top+1][i].f;
                            else {
                                // skip ahead until after assignment
                                found = 0;
                                tok++;
                                count++;
                                while (count < expr->length && tok->toktype != TOK_END) {
                                    if (tok->toktype == TOK_ASSIGNMENT)
                                        found = 1;
                                    else if (found) {
                                        --tok;
                                        --count;
                                        break;
                                    }
                                    tok++;
                                    count++;
                                }
#if TRACING
                                printf("= ABORT!\n");
#endif
                                goto skip_typecast;
                            }
                        }
                        break;
                    case OP_CONDITIONAL_IF_ELSE:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (!stack[top][i].f)
                                stack[top][i].f = stack[top+1][i].f;
                        }
                        break;
                    case OP_CONDITIONAL_IF_THEN_ELSE:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (stack[top][i].f)
                                stack[top][i].f = stack[top+1][i].f;
                            else
                                stack[top][i].f = stack[top+2][i].f;
                        }
                        break;
                    default: goto error;
                }
            } else if (tok->datatype == 'd') {
                switch (tok->op) {
                    case OP_ADD:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d + stack[top+1][i].d;
                        break;
                    case OP_SUBTRACT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d - stack[top+1][i].d;
                        break;
                    case OP_MULTIPLY:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d * stack[top+1][i].d;
                        break;
                    case OP_DIVIDE:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d / stack[top+1][i].d;
                        break;
                    case OP_MODULO:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = fmod(stack[top][i].d, stack[top+1][i].d);
                        break;
                    case OP_IS_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d == stack[top+1][i].d;
                        break;
                    case OP_IS_NOT_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d != stack[top+1][i].d;
                        break;
                    case OP_IS_LESS_THAN:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d < stack[top+1][i].d;
                        break;
                    case OP_IS_LESS_THAN_OR_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d <= stack[top+1][i].d;
                        break;
                    case OP_IS_GREATER_THAN:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d > stack[top+1][i].d;
                        break;
                    case OP_IS_GREATER_THAN_OR_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d >= stack[top+1][i].d;
                        break;
                    case OP_LOGICAL_AND:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d && stack[top+1][i].d;
                        break;
                    case OP_LOGICAL_OR:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = stack[top][i].d || stack[top+1][i].d;
                        break;
                    case OP_LOGICAL_NOT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = !stack[top][i].d;
                        break;
                    case OP_CONDITIONAL_IF_THEN:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (stack[top][i].d)
                                stack[top][i].d = stack[top+1][i].d;
                            else {
                                // skip ahead until after assignment
                                found = 0; // found assignment
                                tok++;
                                count++;
                                while (count < expr->length && tok->toktype != TOK_END) {
                                    if (tok->toktype == TOK_ASSIGNMENT)
                                        found = 1;
                                    else if (found) {
                                        --tok;
                                        --count;
                                        break;
                                    }
                                    tok++;
                                    count++;
                                }
#if TRACING
                                printf("= ABORT!\n");
#endif
                                goto skip_typecast;
                            }
                        }
                        break;
                    case OP_CONDITIONAL_IF_ELSE:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (!stack[top][i].d)
                                stack[top][i].d = stack[top+1][i].d;
                        }
                        break;
                    case OP_CONDITIONAL_IF_THEN_ELSE:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (stack[top][i].d)
                                stack[top][i].d = stack[top+1][i].d;
                            else
                                stack[top][i].d = stack[top+2][i].d;
                        }
                        break;
                    default: goto error;
                }
            } else {
                switch (tok->op) {
                    case OP_ADD:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 + stack[top+1][i].i32;
                        break;
                    case OP_SUBTRACT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 - stack[top+1][i].i32;
                        break;
                    case OP_MULTIPLY:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 * stack[top+1][i].i32;
                        break;
                    case OP_DIVIDE:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 / stack[top+1][i].i32;
                        break;
                    case OP_MODULO:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 % stack[top+1][i].i32;
                        break;
                    case OP_IS_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 == stack[top+1][i].i32;
                        break;
                    case OP_IS_NOT_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 != stack[top+1][i].i32;
                        break;
                    case OP_IS_LESS_THAN:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 < stack[top+1][i].i32;
                        break;
                    case OP_IS_LESS_THAN_OR_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 <= stack[top+1][i].i32;
                        break;
                    case OP_IS_GREATER_THAN:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 > stack[top+1][i].i32;
                        break;
                    case OP_IS_GREATER_THAN_OR_EQUAL:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 >= stack[top+1][i].i32;
                        break;
                    case OP_LEFT_BIT_SHIFT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 << stack[top+1][i].i32;
                        break;
                    case OP_RIGHT_BIT_SHIFT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 >> stack[top+1][i].i32;
                        break;
                    case OP_BITWISE_AND:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 & stack[top+1][i].i32;
                        break;
                    case OP_BITWISE_OR:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 | stack[top+1][i].i32;
                        break;
                    case OP_BITWISE_XOR:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 ^ stack[top+1][i].i32;
                        break;
                    case OP_LOGICAL_AND:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 && stack[top+1][i].i32;
                        break;
                    case OP_LOGICAL_OR:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = stack[top][i].i32 || stack[top+1][i].i32;
                        break;
                    case OP_LOGICAL_NOT:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = !stack[top][i].i32;
                        break;
                    case OP_CONDITIONAL_IF_THEN:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (stack[top][i].i32)
                                stack[top][i].i32 = stack[top+1][i].i32;
                            else {
                                // skip ahead until after assignment
                                found = 0; // found assignment
                                tok++;
                                count++;
                                while (count < expr->length && tok->toktype != TOK_END) {
                                    if (tok->toktype == TOK_ASSIGNMENT)
                                        found = 1;
                                    else if (found) {
                                        --tok;
                                        --count;
                                        break;
                                    }
                                    tok++;
                                    count++;
                                }
#if TRACING
                                printf("= ABORT!\n");
#endif
                                goto skip_typecast;
                            }
                        }
                        break;
                    case OP_CONDITIONAL_IF_ELSE:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (!stack[top][i].i32)
                                stack[top][i].i32 = stack[top+1][i].i32;
                        }
                        break;
                    case OP_CONDITIONAL_IF_THEN_ELSE:
                        for (i = 0; i < tok->vector_length; i++) {
                            if (stack[top][i].i32)
                                stack[top][i].i32 = stack[top+1][i].i32;
                            else
                                stack[top][i].i32 = stack[top+2][i].i32;
                        }
                        break;
                    default: goto error;
                }
            }
#if TRACING
            printf("= ");
            print_stack_vector(stack[top], tok->datatype, tok->vector_length);
            printf("\n");
#endif
            break;
        case TOK_FUNC:
            top -= function_table[tok->func].arity-1;
            dims[top] = tok->vector_length;
#if TRACING
            printf("%s%c(", function_table[tok->func].name, tok->datatype);
            for (i = 0; i < function_table[tok->func].arity; i++) {
                print_stack_vector(stack[top], tok->datatype, tok->vector_length);
                printf(", ");
            }
            printf("\b\b)");
#endif
            if (tok->datatype == 'f') {
                switch (function_table[tok->func].arity) {
                    case 0:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = ((func_float_arity0*)function_table[tok->func].func_float)();
                        break;
                    case 1:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = ((func_float_arity1*)function_table[tok->func].func_float)(stack[top][i].f);
                        break;
                    case 2:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].f = ((func_float_arity2*)function_table[tok->func].func_float)(stack[top][i].f, stack[top+1][i].f);
                        break;
                    default: goto error;
                }
            }
            else if (tok->datatype == 'd') {
                switch (function_table[tok->func].arity) {
                    case 0:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = ((func_double_arity0*)function_table[tok->func].func_double)();
                        break;
                    case 1:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = ((func_double_arity1*)function_table[tok->func].func_double)(stack[top][i].d);
                        break;
                    case 2:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].d = ((func_double_arity2*)function_table[tok->func].func_double)(stack[top][i].d, stack[top+1][i].d);
                        break;
                    default: goto error;
                }
            }
            else if (tok->datatype == 'i') {
                switch (function_table[tok->func].arity) {
                    case 0:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = ((func_int32_arity0*)function_table[tok->func].func_int32)();
                        break;
                    case 1:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = ((func_int32_arity1*)function_table[tok->func].func_int32)(stack[top][i].i32);
                        break;
                    case 2:
                        for (i = 0; i < tok->vector_length; i++)
                            stack[top][i].i32 = ((func_int32_arity2*)function_table[tok->func].func_int32)(stack[top][i].i32, stack[top+1][i].i32);
                        break;
                    default: goto error;
                }
            }
#if TRACING
            printf("= ");
            print_stack_vector(stack[top], tok->datatype, tok->vector_length);
            printf("\n");
#endif
            break;
        case TOK_VFUNC:
#if TRACING
            printf("%s%c(", vfunc_strings[tok->func], tok->datatype);
            print_stack_vector(stack[top], tok->datatype, dims[top]);
            printf(")");
#endif
            if (tok->datatype == 'f') {
                if (tok->func == VFUNC_ALL) {
                    found = 1;
                    for (i = 0; i < dims[top]; i++) {
                        if (stack[top][i].f == 0) {
                            found = 0;
                            break;
                        }
                    }
                }
                else if (tok->func == VFUNC_ANY) {
                    found = 0;
                    for (i = 0; i < dims[top]; i++)
                        if (stack[top][i].f != 0) {
                            found = 1;
                            break;
                        }
                }
                else
                    goto error;
                for (i=0; i < tok->vector_length; i++)
                    stack[top][i].f = (float)found;
            }
            else if (tok->datatype == 'i') {
                if (tok->func == VFUNC_ALL) {
                    found = 1;
                    for (i = 0; i < dims[top]; i++) {
                        if (stack[top][i].i32 == 0) {
                            found = 0;
                            break;
                        }
                    }
                }
                else if (tok->func == VFUNC_ANY) {
                    found = 0;
                    for (i = 0; i < dims[top]; i++)
                        if (stack[top][i].i32 != 0) {
                            found = 1;
                            break;
                        }
                }
                else
                    goto error;
                for (i=0; i < tok->vector_length; i++)
                    stack[top][i].i32 = found;
            }
            else if (tok->datatype == 'd') {
                if (tok->func == VFUNC_ALL) {
                    found = 1;
                    for (i = 0; i < dims[top]; i++) {
                        if (stack[top][i].d == 0) {
                            found = 0;
                            break;
                        }
                    }
                }
                else if (tok->func == VFUNC_ANY) {
                    found = 0;
                    for (i = 0; i < dims[top]; i++)
                        if (stack[top][i].d != 0) {
                            found = 1;
                            break;
                        }
                }
                else
                    goto error;
                for (i=0; i < tok->vector_length; i++)
                    stack[top][i].d = (double)found;
            }
            dims[top] = tok->vector_length;
#if TRACING
            printf("= ");
            print_stack_vector(stack[top], tok->datatype, tok->vector_length);
            printf("\n");
#endif
            break;
        case TOK_VECTORIZE:
            // don't need to copy vector elements from first token
            top -= tok->vectorizer_arity-1;
            k = dims[top];
            if (tok->datatype == 'f') {
                for (i = 1; i < tok->vectorizer_arity; i++) {
                    for (j = 0; j < dims[top+1]; j++)
                        stack[top][k++].f = stack[top+i][j].f;
                }
            }
            else if (tok->datatype == 'i') {
                for (i = 1; i < tok->vectorizer_arity; i++) {
                    for (j = 0; j < dims[top+1]; j++)
                        stack[top][k++].i32 = stack[top+i][j].i32;
                }
            }
            else if (tok->datatype == 'd') {
                for (i = 1; i < tok->vectorizer_arity; i++) {
                    for (j = 0; j < dims[top+1]; j++)
                        stack[top][k++].d = stack[top+i][j].d;
                }
            }
            dims[top] = tok->vector_length;
#if TRACING
            printf("built %i-element vector: ", tok->vector_length);
            print_stack_vector(stack[top], tok->datatype, tok->vector_length);
            printf("\n");
#endif
            break;
        case TOK_ASSIGNMENT:
            if (tok->var == VAR_Y) {
                int idx = (tok->history_index + to->position + to->size);
                if (idx < 0)
                    idx = to->size - idx;
                else
                    idx %= to->size;

                if (to->type == 'f') {
                    float *v = to->value + idx * to->length * mapper_type_size(to->type);
                    for (i = 0; i < tok->vector_length; i++)
                        v[i + tok->vector_index] = stack[top][i + tok->assignment_offset].f;
                }
                else if (to->type == 'i') {
                    int *v = to->value + idx * to->length * mapper_type_size(to->type);
                    for (i = 0; i < tok->vector_length; i++)
                        v[i + tok->vector_index] = stack[top][i + tok->assignment_offset].i32;
                }
                else if (to->type == 'd') {
                    double *v = to->value + idx * to->length * mapper_type_size(to->type);
                    for (i = 0; i < tok->vector_length; i++)
                        v[i + tok->vector_index] = stack[top][i + tok->assignment_offset].d;
                }

                if (typestring) {
                    for (i = tok->vector_index; i < tok->vector_index + tok->vector_length; i++) {
                        typestring[i] = tok->datatype;
                    }
                }

                // Also copy timetag from input
                mapper_timetag_t *ttfrom = msig_history_tt_pointer(*from);
                mapper_timetag_t *ttto = &to->timetag[idx];
                memcpy(ttto, ttfrom, sizeof(mapper_timetag_t));

                // Mark updated flag for tracking output history index increment
                updated++;

#if TRACING
                printf("assigning values to variable y{%d}[%d",
                       tok->history_index, tok->vector_index);
                if (tok->vector_length > 1)
                    printf(":%d", tok->vector_length + tok->vector_index - 1);
                printf("]\n");
#endif
            }
            else if (tok->var >= 0 && tok->var < expr->num_variables + N_VARS) {
                // passed the address of an array of mapper_signal_history structs
                mapper_signal_history_t *h = *expr_vars + (tok->var - N_VARS);
                // increment position
                h->position = (h->position + 1) % h->size;

                mapper_variable var = &expr->variables[tok->var - N_VARS];
                int idx = (tok->history_index + h->position
                           + var->history_size) % var->history_size;
                double *v = h->value + idx * var->vector_length * mapper_type_size(var->datatype);
                for (i = 0; i < tok->vector_length; i++)
                    v[i + tok->vector_index] = stack[top][i + tok->assignment_offset].d;

                // Also copy timetag from input
                mapper_timetag_t *ttfrom = msig_history_tt_pointer(*from);
                mapper_timetag_t *ttvar = msig_history_tt_pointer(*h);
                memcpy(ttvar, ttfrom, sizeof(mapper_timetag_t));

#if TRACING
                printf("assigning values to variable %s{%d}[%d", var->name,
                       tok->history_index, tok->vector_index);
                if (tok->vector_length > 1)
                    printf(":%d", tok->vector_length + tok->vector_index - 1);
                printf("]\n");
#endif
            }

            /* If assignment was history initialization, move expression start
             * token pointer so we don't evaluate this section again. */
            if (tok->history_index != 0) {
                int offset = tok - expr->start + 1;
                expr->start = tok+1;
                expr->length -= offset;
                count -= offset;
#if TRACING
                printf("advancing expression start to token %d\n", offset);
#endif
            }
            break;
        case TOK_TT_ASSIGNMENT:
            if (tok->var == VAR_Y) {
                int idx = (tok->history_index + to->position + to->size);
                if (idx < 0)
                    idx = to->size - idx;
                else
                    idx %= to->size;

                mapper_timetag_set_double(&to->timetag[idx], stack[top][0].d);

                // Mark updated flag for tracking output history index increment
                updated++;

#if TRACING
                printf("assigning values to variable timetag y{%d}.tt\n",
                       tok->history_index);
#endif
            }

            /* If assignment was history initialization, move expression start
             * token pointer so we don't evaluate this section again. */
            if (tok->history_index != 0) {
                int offset = tok - expr->start + 1;
                expr->start = tok+1;
                expr->length -= offset;
                count -= offset;
#if TRACING
                printf("advancing expression start to token %d\n", offset);
#endif
            }
            break;
        default: goto error;
        }
        if (tok->casttype) {
#if TRACING
            printf("casting from %c to %c\n", tok->datatype, tok->casttype);
#endif
            // need to cast to a different type
            if (tok->datatype == 'i') {
                if (tok->casttype == 'f') {
                    for (i = 0; i < tok->vector_length; i++) {
                        stack[top][i].f = (float)stack[top][i].i32;
                    }
                }
                else if (tok->casttype == 'd') {
                    for (i = 0; i < tok->vector_length; i++) {
                        stack[top][i].d = (double)stack[top][i].i32;
                    }
                }
            }
            else if (tok->datatype == 'f') {
                if (tok->casttype == 'i') {
                    for (i = 0; i < tok->vector_length; i++) {
                        stack[top][i].i32 = (int)stack[top][i].f;
                    }
                }
                else if (tok->casttype == 'd') {
                    for (i = 0; i < tok->vector_length; i++) {
                        stack[top][i].d = (double)stack[top][i].f;
                    }
                }
            }
            else if (tok->datatype == 'd') {
                if (tok->casttype == 'i') {
                    for (i = 0; i < tok->vector_length; i++) {
                        stack[top][i].i32 = (int)stack[top][i].d;
                    }
                }
                else if (tok->casttype == 'f') {
                    for (i = 0; i < tok->vector_length; i++) {
                        stack[top][i].f = (float)stack[top][i].d;
                    }
                }
            }
        }
    skip_typecast:
        tok++;
        count++;
    }

    if (!typestring) {
        /* Internal evaluation during parsing doesn't contain assignment token,
         * so we need to copy to output here. */

        /* Increment index position of output data structure. */
        to->position = (to->position + 1) % to->size;

        if (to->type == 'f') {
            float *v = msig_history_value_pointer(*to);
            for (i = 0; i < to->length; i++)
                v[i] = stack[top][i].f;
        }
        else if (to->type == 'i') {
            int *v = msig_history_value_pointer(*to);
            for (i = 0; i < to->length; i++)
                v[i] = stack[top][i].i32;
        }
        else if (to->type == 'd') {
            double *v = msig_history_value_pointer(*to);
            for (i = 0; i < to->length; i++)
                v[i] = stack[top][i].d;
        }
        return 1;
    }

    /* Undo position increment if nothing was updated. */
    if (!updated) {
        --to->position;
        if (to->position < 0)
            to->position = to->size - 1;
        return 0;
    }

    return 1;

  error:
    trace("Unexpected token in expression.");
    return 0;
}
