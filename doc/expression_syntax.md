libmapper Expression Syntax
===========================

Connections between signals that are maintained by libmapper
can be configured with optional signal processing in the
form of an expression.

General Syntax
--------------

Expressions in libmapper must always be presented in the form
`y = x`, where `x` refers to the updated source value and `y` is
the computed value to be forwarded to the destination.


Available Functions
-------------------

### Absolute value:

* `y = abs(x)` — absolute value

### Exponential functions:
* `y = exp(x)` — returns e raised to the given power
* `y = exp2(x)` — returns 2 raised to the given power
* `y = log(x)` — computes natural ( base e ) logarithm ( to base e )
* `y = log10(x)` — computes common ( base 10 ) logarithm
* `y = log2(x)` – computes the binary ( base 2 ) logarithm
* `y = logb(x)` — extracts exponent of the number

### Power functions:
* `y = sqrt(x)` — square root
* `y = cbrt(x)` — cubic root
* `y = hypot(x, n)` — square root of the sum of the squares of two given numbers
* `y = pow(x, n)` — raise a to the power b

### Trigonometric functions:
* `y = sin(x)` — sine
* `y = cos(x)` — cosine
* `y = tan(x)` — tangent
* `y = asin(x)` — arc sine
* `y = acos(x)` — arc cosine
* `y = atan(x)` — arc tangent
* `y = atan2(x, n)` — arc tangent, using signs to determine quadrants

### Hyperbolic functions:
* `y = sinh(x)` — hyperbolic sine
* `y = cosh(x)` — hyperbolic cosine
* `y = tanh(x)` — hyperbolic tangent

### Nearest integer floating point comparisons:
* `y = floor(x)` — nearest integer not greater than the given value
* `y = round(x)` — nearest integer, rounding away from zero in halfway cases
* `y = ceil(x)` — nearest integer not less than the given value
* `y = trunc(x)` — nearest integer not greater in magnitude than the given value

### Comparison functions:
* `y = min(x, n)` – smaller of two values
* `y = max(x, n)` – greater of two values

### Random number generation:
* `y = uniform(x)` — uniform random distribution between 0 and the given value

### Conversion functions:
* `y = midiToHz(x)` — convert MIDI note value to Hz
* `y = hzToMidi(x)` — convert Hz value to MIDI note

Vectors
=======

Individual elements of variable values can be accessed using the notation
`<variable>[<index>]`. The index specifies the vector element, and
obviously must be `>=0`. Expressions must match the vector lengths of the
source and destination signals, and can be used to translate between
signals with different vector lengths.

### Vector examples

* `y = x[0]` — simple vector indexing
* `y = x[1:2]` — specify a range within the vector
* `y = [x[1], x[2], x[0]]` — rearranging ("swizzling") vector elements
* `y[1] = x` — apply update to a specific element of the output
* `y[0:2] = x` — apply update to elements `0-2` of the output vector
* `[y[0], y[2]] = x` — apply update to output vector elements `y[0]` and `y[2]` but
leave `y[1]` unchanged.

### Vector functions

There are two special functions that operate across all elements of the vector:

* `y = any(x)` — output `1` if **any** of the elements of `x` are non-zero, otherwise output `0`
* `y = all(x)` — output `1` if **all** of the elements of `x` are non-zero, otherwise output `0`

FIR and IIR Filters
===================

Past samples of expression input and output can be accessed using the notation `<var>{-n}` where `<var>` is the variable name and `n` is the history index. The index specifies the history index in samples, and must be `<=0` for the input (with `0` representing the present input sample) and `<0` for the expression output (i.e. it cannot be a value that has not been provided or computed yet).

Using only past samples of the expression *input* `x` we can create **Finite
Impulse Response** (FIR) filters - here are some simple examples:

* `y = x - x{-1}` — 2-sample derivative
* `y = x + x{-1}` — 2-sample integral

Using past samples of the expression *output* `y` we can create **Infinite
Impulse Response** (IIR) filters - here are some simple examples:

* `y = y{-1} * 0.9 + x * 0.1` — exponential moving average
* `y = y{-1} + x - 1` — leaky integrator with a constant leak of `1`

Currently libmapper will reject expressions referring to sample delays `> 100`.

Initializing filters
--------------------
Past values of the filter output `y{-n}` can be set using additional sub-expressions, separated using commas:

* `y = y{-1} + x`, `y{-1} = 100`

Filter initialization takes place the first time the expression evaluator is called;
after this point the initialization sub-expressions will not be evaluated. This means
the filter could be initialized with the first sample of `x` for example:

* `y = y{-1} + x`, `y{-1} = x * 2`

A function could also be used for initialization:

* `y = y{-1} + x`, `y{-1} = uniform(1000)` — initialize `y{-1}` to a random value

Any past values that are not explicitly initialized are given the value `0`.


User-Declared Variables
=======================

Additional variables can be declared as-needed in the expression (currently up to 8). The variable names can be any string except for the reserved variable names `x` and `y` or the name of an existing function.  The values of these variables are stored with the connection context and can be accessed in subsequent calls to the evaluator. In the following example, the user-defined variable `ema` is used to keep track of the `exponential moving average` of the input signal value `x`, *independent* of the output value `y` which is set to give the difference between the current sample and the moving average:

* `ema = ema{-1} * 0.9 + x * 0.1`, `y = x - ema`

Just like the output variable `y` we can initialize past values of user-defined variables before expression evaluation. **Initialization will always be performed first**, after which sub-expressions are evaluated **in the order they are written**. For example, the expression string `y=ema*2, ema=ema{-1}*0.9+x*0.1, ema{-1}=90` will be evaluated in the following order:

1. `ema{-1}=90` — initialize the past value of variable `ema` to `90`
2. `y=ema*2` — set output variable `y` to equal the **current** value of `ema` multiplied by `2`. The current value of `ema` is `0` since it has not yet been set.
3. `ema=ema{-1}*0.9+x*0.1` — set the current value of `ema` using current value of `x` and the past value of `ema`

Accessing Variable Timetags
===========================

The precise time at which a variable is updated is always tracked by libmapper and communicated over connections with the data value. In the background this information can be used for discarding out-of-order packets and jitter mitigation but it may also be useful in your expressions.

Currently variable timetags can be accessed using the syntax `<variable_name>.tt` – for example the time associated with the current sample `x` is `x.tt`, and the timetag associated with the last update of a hypothetical user-defined variable `foo` would be `foo.tt`. This syntax can be used anywhere in your expressions:

* `y=x.tt` — output the timetag of the input instead of its value
* `y=x.tt-x{-1}.tt` — output the time interval between subsequent updates

This functionality can be used to limit the output rate:

* `y=(x.tt-y{-1}.tt)>0.5?x` — only output if more than 0.5 seconds has elapsed since the last output, otherwise discard input sample

Also we can calculate a moving average of the sample period:

* `y=y{-1}*0.9+(x.tt-y{-1}.tt)*0.1`

Of course this moving average will start with a very large first value for `(x.tt-y{-1}.tt)` since the first value for `y{-1}.tt` will be `0`. We can easily fix this by initializing the first value for `y{-1}.tt` – remember from above that this part of the expression will only be called once so it will not adversely affect the efficiency of out expression:

* `y{-1}.tt=x.tt,` `y=y{-1}*0.9+(x.tt-y{-1}.tt)*0.1`

Here's a more complex example with 4 sub-expressions in which the rate is limited but incoming samples are averaged instead of discarding them:

* `A=(x.tt-y{-1}.tt)>0.1,` `y=A?B/C,` `B=!A*B+x,` `C=A?1:C+1;`

In this case:

* Variable `A` equals 1 if more than 0.1 seconds have elapsed since the last output, and 0 otherwise.
* Variable `B` accumulates updates to `x` (our input).
* Variable `C` is a counter tracking how many updates have been accumulated.
* Variable `y` (our output) is updated to `B/C` (aggregate/count), but only when `A != 0`.
* After each update to `y` (when `A == 1`) the variables `B` and `C` are reset.
