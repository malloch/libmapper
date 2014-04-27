
#include <../src/mapper_internal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define DEST_ARRAY_LEN 6
#define MAX_VARS 3

char str[256];
mapper_expr e;
int result = 0;
int iterations = 1000000;
int testcounter = 1;

int src_int[] = {1, 2, 3}, dest_int[DEST_ARRAY_LEN];
float src_float[] = {1.0f, 2.0f, 3.0f}, dest_float[DEST_ARRAY_LEN];
double src_double[] = {1.0, 2.0, 3.0}, dest_double[DEST_ARRAY_LEN];
double then, now;
char typestring[3];

mapper_timetag_t tt_in = {0, 0}, tt_out = {0, 0};

// signal_history structures
mapper_signal_history_t inh, outh, user_vars[MAX_VARS], *user_vars_p;

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

typedef struct _variable {
    char *name;
    int vector_index;
    int vector_length;
    char datatype;
    char casttype;
    char history_size;
    char vector_length_locked;
    char assigned;
} mapper_variable_t, *mapper_variable;

struct _mapper_expr
{
    void *tokens;
    void *start;
    int length;
    int vector_size;
    int input_history_size;
    int output_history_size;
    mapper_variable variables;
    int num_variables;
};

/* TODO:
 multiplication by 0
 addition/subtraction of 0
 division by 0
 */

void print_value(char *types, int length, const void *value, int position)
{
    if (!value || !types || length < 1)
        return;

    if (length > 1)
        printf("[");

    int i, offset = position * length;
    for (i = 0; i < length; i++) {
        switch (types[i]) {
            case 'N':
                printf("NULL, ");
                break;
            case 'f':
            {
                float *pf = (float*)value;
                printf("%g, ", pf[i + offset]);
                break;
            }
            case 'i':
            {
                int *pi = (int*)value;
                printf("%d, ", pi[i + offset]);
                break;
            }
            case 'd':
            {
                double *pd = (double*)value;
                printf("%g, ", pd[i + offset]);
                break;
            }
            default:
                printf("\nTYPE ERROR\n");
                return;
        }
    }

    if (length > 1)
        printf("\b\b]");
    else
        printf("\b\b");
}

void setup_test(char in_type, int in_length, void *in_value,
                char out_type, int out_length, void *out_value)
{
    inh.type = in_type;
    inh.length = in_length;
    inh.value = in_value;
    inh.position = 0;
    inh.timetag = &tt_in;

    outh.type = out_type;
    outh.length = out_length;
    outh.value = out_value;
    outh.position = -1;
    outh.timetag = &tt_out;
}

int parse_and_eval()
{
    // need a mapper_clock for testing timetags
    mapper_clock_t c;
    mapper_clock_init(&c);

    // clear output arrays
    int i;
    for (i = 0; i < DEST_ARRAY_LEN; i++) {
        dest_int[i] = 0;
        dest_float[i] = 0.0f;
        dest_double[i] = 0.0;
    }

    printf("**********************************\n");
    printf("Parsing string %d: '%s'\n", testcounter++, str);
    if (!(e = mapper_expr_new_from_string(str, inh.type, outh.type,
                                          inh.length, outh.length))) {
        printf("Parser FAILED.\n");
        return 1;
    }
    inh.size = mapper_expr_input_history_size(e);
    outh.size = mapper_expr_output_history_size(e);

    if (mapper_expr_num_variables(e) > MAX_VARS) {
        printf("Maximum variables exceeded.\n");
        return 1;
    }

    // reallocate variable value histories
    for (i = 0; i < e->num_variables; i++) {
        mhist_realloc(&user_vars[i], e->variables[i].history_size,
                      e->variables[i].vector_length * sizeof(double), 0);
        memset(user_vars[i].value, 0, e->variables[i].history_size *
               e->variables[i].vector_length * sizeof(double));
    }
    user_vars_p = user_vars;

#ifdef DEBUG
    printexpr("Parser returned:", e);
#endif

    printf("Try evaluation once... ");
    mapper_clock_now(&c, &inh.timetag[inh.position]);
    if (!mapper_expr_evaluate(e, &inh, &user_vars_p, &outh, typestring)) {
        printf("FAILURE.\n");
        return 1;
    }
    printf("SUCCESS.\n");

    printf("Calculate expression %i more times... ", iterations-1);
    i = iterations-1;
    then = get_current_time();
    while (i--) {
        // update history position index of input
        inh.position = (inh.position + 1) % inh.size;
        mapper_clock_now(&c, &inh.timetag[inh.position]);
        mapper_expr_evaluate(e, &inh, &user_vars_p, &outh, typestring);
    }
    now = get_current_time();
    printf("%g seconds.\n", now-then);

    printf("Got:      ");
    print_value(typestring, outh.length, outh.value, outh.position);
    printf(" \n");

    mapper_expr_free(e);
    return 0;
}

int main()
{
    int result = 0;

    /* 1) Complex string */
    snprintf(str, 256, "y=26*2/2+log10(pi)+2.*pow(2,1*(3+7*.1)*1.1+x{0}[0])*3*4+cos(2.)");
    setup_test('f', 1, src_float, 'f', 1, dest_float);
    result += parse_and_eval();
    printf("Expected: %g\n", 26*2/2+log10f(M_PI)+2.f*powf(2,1*(3+7*.1f)*1.1f+src_float[0])*3*4+cosf(2.0f));

    /* 2) Building vectors, conditionals */
    snprintf(str, 256, "y=(x>1)?[1,2,3]:[2,4,6]");
    setup_test('f', 3, src_float, 'i', 3, dest_int);
    result += parse_and_eval();
    printf("Expected: [%i, %i, %i]\n", src_float[0]>1?1:2, src_float[1]>1?2:4,
           src_float[2]>1?3:6);

    /* 3) Conditionals with shortened syntax */
    snprintf(str, 256, "y=x?:123");
    setup_test('f', 1, src_float, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: %i\n", (int)src_float[0]?:123);

    /* 4) Conditional that should be optimized */
    snprintf(str, 256, "y=1?2:123");
    setup_test('f', 1, src_float, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: 2\n");

    /* 5) Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[x*-2+1,0]");
    setup_test('i', 2, src_int, 'd', 3, dest_double);
    result += parse_and_eval();
    printf("Expected: [%g, %g, %g]\n", (double)src_int[0]*-2+1,
           (double)src_int[1]*-2+1, 0.0);

    /* 6) Building vectors with variables, operations inside vector-builder */
    snprintf(str, 256, "y=[-99.4, -x*1.1+x]");
    setup_test('i', 2, src_int, 'd', 3, dest_double);
    result += parse_and_eval();
    printf("Expected: [%g, %g, %g]\n", -99.4,
           (double)(-src_int[0]*1.1+src_int[0]),
           (double)(-src_int[1]*1.1+src_int[1]));

    /* 7) Indexing vectors by range */
    snprintf(str, 256, "y=x[1:2]+100");
    setup_test('d', 3, src_double, 'f', 2, dest_float);
    result += parse_and_eval();
    printf("Expected: [%g, %g]\n", (float)src_double[1]+100,
           (float)src_double[2]+100);

    /* 8) Typical linear scaling expression with vectors */
    snprintf(str, 256, "y=x*[0.1,3.7,-.1112]+[2,1.3,9000]");
    setup_test('f', 3, src_float, 'f', 3, dest_float);
    result += parse_and_eval();
    printf("Expected: [%g, %g, %g]\n", src_float[0]*0.1f+2.f,
           src_float[1]*3.7f+1.3f, src_float[2]*-.1112f+9000.f);

    /* 9) Check type and vector length promotion of operation sequences */
    snprintf(str, 256, "y=1+2*3-4*x");
    setup_test('f', 2, src_float, 'f', 2, dest_float);
    result += parse_and_eval();
    printf("Expected: [%g, %g]\n", 1.f+2.f*3.f-4.f*src_float[0],
           1.f+2.f*3.f-4.f*src_float[1]);

    /* 10) Swizzling, more pre-computation */
    snprintf(str, 256, "y=[x[2],x[0]]*0+1+12");
    setup_test('f', 3, src_float, 'f', 2, dest_float);
    result += parse_and_eval();
    printf("Expected: [%g, %g]\n", src_float[2]*0.f+1.f+12.f,
           src_float[0]*0.f+1.f+12.f);

    /* 11) Logical negation */
    snprintf(str, 256, "y=!(x[1]*0)");
    setup_test('d', 3, src_double, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: %i\n", (int)!(src_double[1]*0));

    /* 12) any() */
    snprintf(str, 256, "y=any(x-1)");
    setup_test('d', 3, src_double, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: %i\n", ((int)src_double[0]-1)?1:0
           | ((int)src_double[1]-1)?1:0
           | ((int)src_double[2]-1)?1:0);

    /* 13) all() */
    snprintf(str, 256, "y=x[2]*all(x-1)");
    setup_test('d', 3, src_double, 'i', 1, dest_int);
    result += parse_and_eval();
    int temp = ((int)src_double[0]-1)?1:0 & ((int)src_double[1]-1)?1:0
                & ((int)src_double[2]-1)?1:0;
    printf("Expected: %i\n", (int)src_double[2] * temp);

    /* 14) pi and e, extra spaces */
    snprintf(str, 256, "y=x + pi -     e");
    setup_test('d', 1, src_double, 'f', 1, dest_float);
    result += parse_and_eval();
    printf("Expected: %g\n", (float)(src_double[0]+M_PI-M_E));

    /* 15) bad vector notation */
    snprintf(str, 256, "y=(x-2)[1]");
    setup_test('i', 1, src_int, 'i', 1, dest_int);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 16) vector index outside bounds */
    snprintf(str, 256, "y=x[3]");
    setup_test('i', 3, src_int, 'i', 1, dest_int);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 17) vector length mismatch */
    snprintf(str, 256, "y=x[1:2]");
    setup_test('i', 3, src_int, 'i', 1, dest_int);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 18) unnecessary vector notation */
    snprintf(str, 256, "y=x+[1]");
    setup_test('i', 1, src_int, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: %i\n", src_int[0]+1);

    /* 19) invalid history index */
    snprintf(str, 256, "y=x{-101}");
    setup_test('i', 1, src_int, 'i', 1, dest_int);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 20) invalid history index */
    snprintf(str, 256, "y=x-y{-101}");
    setup_test('i', 1, src_int, 'i', 1, dest_int);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 21) scientific notation */
    snprintf(str, 256, "y=x[1]*1.23e-20");
    setup_test('i', 2, src_int, 'd', 1, dest_double);
    result += parse_and_eval();
    printf("Expected: %g\n", (double)src_int[1] * 1.23e-20);

    /* 22) Vector assignment */
    snprintf(str, 256, "y[1]=x[1]");
    setup_test('d', 3, src_double, 'i', 3, dest_int);
    result += parse_and_eval();
    printf("Expected: [NULL, %i, NULL]\n", (int)src_double[1]);

    /* 23) Vector assignment */
    snprintf(str, 256, "y[1:2]=[x[1],10]");
    setup_test('d', 3, src_double, 'i', 3, dest_int);
    result += parse_and_eval();
    printf("Expected: [NULL, %i, %i]\n", (int)src_double[1], 10);

    /* 24) Output vector swizzling */
    snprintf(str, 256, "[y[0],y[2]]=x[1:2]");
    setup_test('f', 3, src_float, 'd', 3, dest_double);
    result += parse_and_eval();
    printf("Expected: [%g, NULL, %g]\n", (double)src_float[1],
           (double)src_float[2]);

    /* 25) Multiple expressions */
    snprintf(str, 256, "y[0]=x*100-23.5, y[2]=100-x*6.7");
    setup_test('i', 1, src_int, 'f', 3, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 26) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}, y{-1}=100");
    setup_test('i', 1, src_int, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: %i\n", src_int[0]*iterations + 100);

    /* 27) Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}, y[1]{-1}=100");
    setup_test('i', 2, src_int, 'i', 2, dest_int);
    result += parse_and_eval();
    printf("Expected: [%i, %i]\n", src_int[0]*iterations,
           src_int[1]*iterations + 100);

    /* 28) Initialize filters + vector index */
    snprintf(str, 256, "y=x+y{-1}, y{-1}=[100,101]");
    setup_test('i', 2, src_int, 'i', 2, dest_int);
    result += parse_and_eval();
    printf("Expected: [%i, %i]\n", src_int[0]*iterations + 100,
           src_int[1]*iterations + 101);

    /* 29) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}, y[0]{-1}=100, y[2]{-1}=200");
    setup_test('i', 3, src_int, 'i', 3, dest_int);
    result += parse_and_eval();
    printf("Expected: [%i, %i, %i]\n", src_int[0]*iterations + 100,
           src_int[1]*iterations, src_int[2]*iterations + 200);

    /* 30) Initialize filters */
    snprintf(str, 256, "y=x+y{-1}-y{-2}, y{-1}=[100,101], y{-2}=[100,101]");
    setup_test('i', 2, src_int, 'i', 2, dest_int);
    result += parse_and_eval();
    printf("Expected: [1, 2]\n");

    /* 31) Only initialize */
    snprintf(str, 256, "y{-1}=100");
    setup_test('i', 3, src_int, 'i', 1, dest_int);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 32) Bad syntax */
    snprintf(str, 256, " ");
    setup_test('i', 1, src_int, 'f', 3, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 33) Bad syntax */
    snprintf(str, 256, "y");
    setup_test('i', 1, src_int, 'f', 3, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 34) Bad syntax */
    snprintf(str, 256, "y=");
    setup_test('i', 1, src_int, 'f', 3, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 35) Bad syntax */
    snprintf(str, 256, "=x");
    setup_test('i', 1, src_int, 'f', 3, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 36) Bad syntax */
    snprintf(str, 256, "sin(x)");
    setup_test('i', 1, src_int, 'f', 3, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 37) Variable declaration */
    snprintf(str, 256, "var=3.5, y=x+var");
    setup_test('i', 1, src_int, 'f', 1, dest_float);
    result += parse_and_eval();
    printf("Expected: %g\n", (float)src_int[0] + 3.5);

    /* 38) Variable declaration */
    snprintf(str, 256, "ema=ema{-1}*0.9+x*0.1, y=ema*2, ema{-1}=90");
    setup_test('i', 1, src_int, 'f', 1, dest_float);
    result += parse_and_eval();
    printf("Expected: 2\n");

    /* 39) Malformed variable declaration */
    snprintf(str, 256, "y=x + myvariable * 10");
    setup_test('i', 1, src_int, 'f', 1, dest_float);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 40) Access timetags */
    snprintf(str, 256, "y=x.tt");
    setup_test('i', 1, src_int, 'd', 1, dest_double);
    result += parse_and_eval();
    printf("Expected: ????\n");

    /* 41) Access timetags from past samples */
    snprintf(str, 256, "y=x.tt-y{-1}.tt");
    setup_test('i', 1, src_int, 'd', 1, dest_double);
    result += parse_and_eval();
    printf("Expected: ????\n");

    /* 42) Moving average of inter-sample period */
    // tricky! we need to skip the first tt diff, but y{-1} needs to be 0
    snprintf(str, 256, "period=counter?x.tt-x{-1}.tt:0, y=y{-1}*0.9+period*0.1, counter=counter+1");
    setup_test('i', 1, src_int, 'd', 1, dest_double);
    result += parse_and_eval();
    printf("Expected: ????\n");

    /* 43) Moving average of inter-sample jitter */
    snprintf(str, 256,
             "interval=counter?x.tt-x{-1}.tt:0,"
             "sr=sr{-1}*0.9+interval*0.1,"
             "y=y{-1}*0.9+(interval-sr)*0.1,"
             "counter=counter+1");
    setup_test('i', 1, src_int, 'd', 1, dest_double);
    result += parse_and_eval();
    printf("Expected: ????\n");

    /* 44) Malformed timetag syntax */
    snprintf(str, 256, "y=x.tt{-1}");
    setup_test('i', 1, src_int, 'd', 1, dest_double);
    result += !parse_and_eval();
    printf("Expected: FAILURE\n");

    /* 45) Expression for limiting output rate */
    snprintf(str, 256, "diff=x.tt-y{-1}.tt, y=!counter||(diff>0.1)?x, counter=counter+1");
    setup_test('i', 1, src_int, 'i', 1, dest_int);
    result += parse_and_eval();
    printf("Expected: 1 or NULL\n");

    printf("**********************************\n");
    printf("Failed %d tests\n", result);
    return result;
}
